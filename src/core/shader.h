#pragma once
#include "engine_config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "logs.h"

// Uniform setter dispatch. The pre-4.1 path leaves this program bound
// afterwards (documented behaviour; restoring per call would stall the driver).
#if ENGINE_HAS_PROGRAM_UNIFORM
    #define ENGINE_SET_UNIFORM(name, ...) glProgram##name(_id, __VA_ARGS__)
#else
    #define ENGINE_SET_UNIFORM(name, ...) do {use(); gl##name(__VA_ARGS__);} while (0)
#endif

// Allocation policy: the helpers here build strings inside noexcept functions;
// allocation failure terminates by design (the engine treats OOM as fatal).
namespace shader_tools {
    // Dedents a raw GLSL block: drops the leading and trailing blank lines and
    // strips the common indent, so embedded source compiles at column zero.
    inline std::string glsl(std::string_view block) noexcept {
        std::vector<std::string_view> lines;
        size_t cursor = 0;
        while (true) {
            const size_t end = block.find('\n', cursor);
            if (end == std::string_view::npos) {lines.push_back(block.substr(cursor)); break;}
            lines.push_back(block.substr(cursor, end - cursor));
            cursor = end + 1;
        }

        const auto blank = [](std::string_view line) {
            return line.find_first_not_of(" \t") == std::string_view::npos;
        };

        size_t first = 0, last = lines.size();
        if (first < last && lines[first].empty()) ++first;
        if (last > first && blank(lines[last - 1])) --last;
        if (first >= last) return "";

        size_t indent = std::string_view::npos;
        for (size_t i = first; i < last; ++i) {
            if (blank(lines[i])) continue;
            indent = std::min(indent, lines[i].find_first_not_of(" \t"));
        }
        if (indent == std::string_view::npos) indent = 0;

        std::string out;
        for (size_t i = first; i < last; ++i) {
            if (!blank(lines[i])) out.append(lines[i].substr(indent));
            out.push_back('\n');
        }
        return out;
    }

    // Rotates a vector by a unit quaternion without building a matrix. Defined
    // here so every generated shader that needs it emits the same one copy.
    inline std::string quaternionRotateGLSL() noexcept {
        return glsl(R"GLSL(

            vec3 rotateByQuaternion(vec4 q, vec3 v) {
                return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
            }
        )GLSL");
    }
}

struct ShaderSource {
    std::filesystem::path file;
    std::string text;
    std::string name;

    // Source that will be read from a file at compile time.
    static ShaderSource fromFile(std::filesystem::path path) noexcept {
        ShaderSource source;
        source.name = path.string();
        source.file = std::move(path);
        return source;
    }

    // Source held directly in memory, with an optional label for error logs.
    static ShaderSource fromText(std::string source_text, std::string label = "<memory>") noexcept {
        ShaderSource source;
        source.text = std::move(source_text);
        source.name = std::move(label);
        return source;
    }
};

struct ShaderConfig {
    std::vector<ShaderSource> vertex;
    std::vector<ShaderSource> fragment;
    #if ENGINE_HAS_GEOMETRY_SHADER
        std::vector<ShaderSource> geometry;
    #endif
    #if ENGINE_HAS_TESS_SHADER
        std::vector<ShaderSource> tess_control;
        std::vector<ShaderSource> tess_evaluation;
    #endif
    #if ENGINE_HAS_COMPUTE_SHADER
        std::vector<ShaderSource> compute;
    #endif

    std::vector<std::string> sampler_names;

    std::vector<std::pair<std::string, unsigned int>> attribute_locations;

    std::vector<std::string> defines;

    #if ENGINE_HAS_PROGRAM_BINARY
        bool retrievable_binary = false;
    #endif
    #if ENGINE_HAS_SEPARATE_PROGRAM
        bool separable = false;
    #endif
};

struct ShaderResource {
    std::string name;
    unsigned int type = 0;
    int location = -1;
    int size = 0;
};

class Shader {
    public:
        // Compiles and links a program from the config; returns null on failure.
        static std::unique_ptr<Shader> create(const ShaderConfig& config) noexcept {
            std::vector<unsigned int> modules;
            const auto cleanup = [&] {for (unsigned int m : modules) glDeleteShader(m);};

            if (!compileStages(config, modules)) {cleanup(); return nullptr;}
            if (modules.empty()) {log_error("No shader sources given"); return nullptr;}

            const unsigned int program = glCreateProgram();
            if (!program) {log_error("Shader: failed to create a program object"); cleanup(); return nullptr;}
            for (unsigned int module : modules) glAttachShader(program, module);

            for (const auto& binding : config.attribute_locations) {
                glBindAttribLocation(program, binding.second, binding.first.c_str());
            }
            #if ENGINE_HAS_PROGRAM_BINARY
                if (config.retrievable_binary) {
                    glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
                }
            #endif
            #if ENGINE_HAS_SEPARATE_PROGRAM
                if (config.separable) glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);
            #endif

            glLinkProgram(program);
            for (unsigned int module : modules) {glDetachShader(program, module); glDeleteShader(module);}
            modules.clear();

            int success = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &success);
            if (!success) {
                log_error("Shader linking error:\n", programLog(program));
                glDeleteProgram(program);
                return nullptr;
            }

            std::unique_ptr<Shader> shader(new Shader(program, isComputeConfig(config)));
            shader->bindSamplers(config.sampler_names);
            return shader;
        }

        // Convenience: one vertex file plus one fragment file, samplers in unit order.
        static std::unique_ptr<Shader> createFromFiles(const std::filesystem::path& vertex,
                                                 const std::filesystem::path& fragment,
                                                 std::vector<std::string> samplers = {}) noexcept {
            ShaderConfig config;
            config.vertex = {ShaderSource::fromFile(vertex)};
            config.fragment = {ShaderSource::fromFile(fragment)};
            config.sampler_names = std::move(samplers);
            return create(config);
        }

        // ---- identity ----

        // The GL program name.
        unsigned int id() const noexcept {return _id;}
        // True when this program is a compute program.
        bool isCompute() const noexcept {return _isCompute;}
        // Makes this the current program.
        void use() const noexcept {glUseProgram(_id);}

        // ---- uniforms ----

        // Cached uniform location lookup; warns once per missing name.
        int uniformLocation(const std::string& name) const noexcept {
            const auto found = _locations.find(name);
            if (found != _locations.end()) return found->second;

            const int location = glGetUniformLocation(_id, name.c_str());
            _locations.emplace(name, location);
            if (location == -1) log_warn("Uniform not found or unused: ", name);
            return location;
        }

        // Sets a mat4 uniform.
        void setMat4(const std::string& n, const glm::mat4& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(UniformMatrix4fv, l, 1, GL_FALSE, glm::value_ptr(v));
        }
        // Sets a mat3 uniform.
        void setMat3(const std::string& n, const glm::mat3& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(UniformMatrix3fv, l, 1, GL_FALSE, glm::value_ptr(v));
        }
        // Sets a mat2 uniform.
        void setMat2(const std::string& n, const glm::mat2& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(UniformMatrix2fv, l, 1, GL_FALSE, glm::value_ptr(v));
        }
        // Sets a vec4 uniform.
        void setVec4(const std::string& n, const glm::vec4& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform4f, l, v.x, v.y, v.z, v.w);
        }
        // Sets a vec3 uniform.
        void setVec3(const std::string& n, const glm::vec3& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform3f, l, v.x, v.y, v.z);
        }
        // Sets a vec2 uniform.
        void setVec2(const std::string& n, const glm::vec2& v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform2f, l, v.x, v.y);
        }
        // Sets a float uniform.
        void setFloat(const std::string& n, float v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform1f, l, v);
        }
        // Sets an int uniform.
        void setInt(const std::string& n, int v) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform1i, l, v);
        }
        #if ENGINE_HAS_UINT_UNIFORM
            // Sets an unsigned int uniform (GL 3.0+).
            void setUInt(const std::string& n, unsigned int v) const noexcept {
                const int l = uniformLocation(n); if (l == -1) return;
                ENGINE_SET_UNIFORM(Uniform1ui, l, v);
            }
        #endif
        // Sets a bool uniform as 0/1.
        void setBool(const std::string& n, bool v) const noexcept {setInt(n, v ? 1 : 0);}

        // Sets a float array uniform.
        void setFloatArray(const std::string& n, const float* values, int count) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform1fv, l, count, values);
        }
        // Sets an int array uniform.
        void setIntArray(const std::string& n, const int* values, int count) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(Uniform1iv, l, count, values);
        }
        // Sets a mat4 array uniform from contiguous matrices.
        void setMat4Array(const std::string& n, const glm::mat4* values, int count) const noexcept {
            const int l = uniformLocation(n); if (l == -1) return;
            ENGINE_SET_UNIFORM(UniformMatrix4fv, l, count, GL_FALSE, glm::value_ptr(values[0]));
        }

        // ---- vertex attributes ----

        // Location of a named vertex attribute, or -1.
        int attributeLocation(const std::string& name) const noexcept {
            return glGetAttribLocation(_id, name.c_str());
        }

        // Every active vertex attribute of the linked program.
        std::vector<ShaderResource> attributes() const noexcept {
            return activeResources(GL_ACTIVE_ATTRIBUTES, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, true);
        }

        // Every active uniform of the linked program.
        std::vector<ShaderResource> uniforms() const noexcept {
            return activeResources(GL_ACTIVE_UNIFORMS, GL_ACTIVE_UNIFORM_MAX_LENGTH, false);
        }

        // ---- validation ----

        // Runs glValidateProgram and logs the info log on failure.
        bool validate() const noexcept {
            glValidateProgram(_id);
            int success = 0;
            glGetProgramiv(_id, GL_VALIDATE_STATUS, &success);
            if (!success) log_error("Program validation failed:\n", programLog(_id));
            return success != 0;
        }

        // ---- blocks ----

        #if ENGINE_HAS_UNIFORM_BLOCKS
            // Binds a named uniform block to a binding point.
            bool bindUniformBlock(const std::string& name, unsigned int bindingPoint) const noexcept {
                const unsigned int index = glGetUniformBlockIndex(_id, name.c_str());
                if (index == GL_INVALID_INDEX) {log_warn("Uniform block not found: ", name); return false;}
                glUniformBlockBinding(_id, index, bindingPoint);
                return true;
            }

            // Byte size of a named uniform block, or -1 if absent.
            int uniformBlockSize(const std::string& name) const noexcept {
                const unsigned int index = glGetUniformBlockIndex(_id, name.c_str());
                if (index == GL_INVALID_INDEX) return -1;
                int size = 0;
                glGetActiveUniformBlockiv(_id, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
                return size;
            }
        #endif

        #if ENGINE_HAS_SSBO
            // Binds a named shader storage block to a binding point.
            bool bindStorageBlock(const std::string& name, unsigned int bindingPoint) const noexcept {
                const unsigned int index = glGetProgramResourceIndex(_id, GL_SHADER_STORAGE_BLOCK, name.c_str());
                if (index == GL_INVALID_INDEX) {log_warn("Storage block not found: ", name); return false;}
                glShaderStorageBlockBinding(_id, index, bindingPoint);
                return true;
            }
        #endif

        #if ENGINE_HAS_PROGRAM_RESOURCE
            // Enumerates the resources of a program interface (GL 4.3 introspection).
            // Type, array size and location are filled only for the interfaces that
            // define them; asking any other interface raises GL_INVALID_OPERATION.
            std::vector<ShaderResource> interfaceResources(unsigned int programInterface) const noexcept {
                std::vector<ShaderResource> out;
                int count = 0;
                glGetProgramInterfaceiv(_id, programInterface, GL_ACTIVE_RESOURCES, &count);
                if (count <= 0) return out;

                int maxLength = 0;
                glGetProgramInterfaceiv(_id, programInterface, GL_MAX_NAME_LENGTH, &maxLength);
                if (maxLength <= 0) maxLength = 256;

                const bool wantsType = interfaceHasType(programInterface);
                const bool wantsLocation = interfaceHasLocation(programInterface);

                std::string buffer(static_cast<size_t>(maxLength), '\0');
                for (int i = 0; i < count; ++i) {
                    int length = 0;
                    glGetProgramResourceName(_id, programInterface, static_cast<unsigned int>(i),
                                             maxLength, &length, buffer.data());
                    ShaderResource resource;
                    resource.name.assign(buffer.data(), static_cast<size_t>(length));

                    if (wantsType) {
                        const GLenum properties[2] = {GL_TYPE, GL_ARRAY_SIZE};
                        GLint values[2] = {0, 0};
                        GLsizei written = 0;
                        glGetProgramResourceiv(_id, programInterface, static_cast<unsigned int>(i),
                                               2, properties, 2, &written, values);
                        if (written >= 1) resource.type = static_cast<unsigned int>(values[0]);
                        if (written >= 2) resource.size = values[1];
                    }
                    if (wantsLocation) {
                        resource.location = glGetProgramResourceLocation(_id, programInterface,
                                                                         resource.name.c_str());
                    }
                    out.push_back(std::move(resource));
                }
                return out;
            }
        #endif

        // ---- subroutines ----

        #if ENGINE_HAS_SUBROUTINES
            // Index of a named subroutine in a stage; warns when absent.
            unsigned int subroutineIndex(unsigned int stage, const std::string& name) const noexcept {
                const unsigned int index = glGetSubroutineIndex(_id, stage, name.c_str());
                if (index == GL_INVALID_INDEX) log_warn("Subroutine not found: ", name);
                return index;
            }

            // Selects the active subroutines for a stage; needs this program current.
            void setSubroutines(unsigned int stage, const std::vector<unsigned int>& indices) const noexcept {
                if (indices.empty()) return;
                use();
                glUniformSubroutinesuiv(stage, static_cast<int>(indices.size()), indices.data());
            }
        #endif

        // ---- compute ----

        #if ENGINE_HAS_COMPUTE_SHADER
            // Dispatches this compute program with the given work-group counts.
            void dispatch(unsigned int x, unsigned int y = 1, unsigned int z = 1) const noexcept {
                if (!_isCompute) {log_error("dispatch called on a non-compute program"); return;}
                use();
                glDispatchCompute(x, y, z);
            }

            // Dispatches with counts read from GL_DISPATCH_INDIRECT_BUFFER.
            void dispatchIndirect(intptr_t offset = 0) const noexcept {
                if (!_isCompute) {log_error("dispatchIndirect called on a non-compute program"); return;}
                use();
                glDispatchComputeIndirect(static_cast<GLintptr>(offset));
            }

            // The program's declared local work-group size.
            void workGroupSize(int out[3]) const noexcept {glGetProgramiv(_id, GL_COMPUTE_WORK_GROUP_SIZE, out);}
        #endif

        #if ENGINE_HAS_MEMORY_BARRIER
            // Issues a memory barrier (GL 4.2, independent of compute: image/SSBO
            // writes from ordinary vertex/fragment stages need barriers too).
            static void barrier(GLbitfield bits = GL_ALL_BARRIER_BITS) noexcept {glMemoryBarrier(bits);}
        #endif

        // ---- binary ----

        #if ENGINE_HAS_PROGRAM_BINARY
            // Number of binary formats the driver exposes (0 = binaries unusable).
            static int binaryFormatCount() noexcept {
                int formats = 0;
                glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
                return formats;
            }

            // Retrieves the linked program's binary and its driver format.
            std::vector<unsigned char> binary(unsigned int& formatOut) const noexcept {
                formatOut = 0;
                if (binaryFormatCount() <= 0) {
                    log_warn("Program binaries unsupported: the driver exposes no binary formats");
                    return {};
                }
                int length = 0;
                glGetProgramiv(_id, GL_PROGRAM_BINARY_LENGTH, &length);
                if (length <= 0) {
                    log_warn("Program binary is empty; set ShaderConfig::retrievable_binary "
                             "before creating the shader");
                    return {};
                }
                std::vector<unsigned char> bytes(static_cast<size_t>(length));
                GLenum format = 0;
                glGetProgramBinary(_id, length, nullptr, &format, bytes.data());
                formatOut = format;
                return bytes;
            }

            // Rebuilds a program from a previously retrieved binary.
            static std::unique_ptr<Shader> createFromBinary(const std::vector<unsigned char>& bytes,
                                                      unsigned int format, bool compute = false) noexcept {
                if (bytes.empty()) {log_error("fromBinary given no data"); return nullptr;}
                const unsigned int program = glCreateProgram();
                glProgramBinary(program, format, bytes.data(), static_cast<int>(bytes.size()));

                int success = 0;
                glGetProgramiv(program, GL_LINK_STATUS, &success);
                if (!success) {
                    log_warn("Program binary rejected; it must be rebuilt from source");
                    glDeleteProgram(program);
                    return nullptr;
                }
                return std::unique_ptr<Shader>(new Shader(program, compute));
            }
        #endif

        // ---- SPIR-V ----

        #if ENGINE_HAS_SPIRV
            // True when the driver exposes at least one shader binary format.
            static bool spirvSupported() noexcept {
                int formats = 0;
                glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &formats);
                return formats > 0;
            }

            // Builds a program from SPIR-V stage blobs, specialised at entryPoint.
            static std::unique_ptr<Shader> createFromSpirv(
                    const std::vector<std::pair<unsigned int, std::vector<uint32_t>>>& stages,
                    const std::string& entryPoint = "main") noexcept {
                if (stages.empty()) {log_error("fromSpirv given no stages"); return nullptr;}

                std::vector<unsigned int> modules;
                bool compute = false;
                for (const auto& stage : stages) {
                    #if ENGINE_HAS_COMPUTE_SHADER
                        if (stage.first == GL_COMPUTE_SHADER) compute = true;
                    #endif
                    const unsigned int module = glCreateShader(stage.first);
                    glShaderBinary(1, &module, GL_SHADER_BINARY_FORMAT_SPIR_V,
                                   stage.second.data(),
                                   static_cast<int>(stage.second.size() * sizeof(uint32_t)));
                    glSpecializeShader(module, entryPoint.c_str(), 0, nullptr, nullptr);

                    int success = 0;
                    glGetShaderiv(module, GL_COMPILE_STATUS, &success);
                    if (!success) {
                        log_error("SPIR-V specialisation failed:\n", shaderLog(module));
                        glDeleteShader(module);
                        for (unsigned int m : modules) glDeleteShader(m);
                        return nullptr;
                    }
                    modules.push_back(module);
                }

                const unsigned int program = glCreateProgram();
                for (unsigned int module : modules) glAttachShader(program, module);
                glLinkProgram(program);
                for (unsigned int module : modules) {glDetachShader(program, module); glDeleteShader(module);}

                int success = 0;
                glGetProgramiv(program, GL_LINK_STATUS, &success);
                if (!success) {
                    log_error("SPIR-V program linking error:\n", programLog(program));
                    glDeleteProgram(program);
                    return nullptr;
                }
                return std::unique_ptr<Shader>(new Shader(program, compute));
            }
        #endif

        ~Shader() noexcept {if (_id) glDeleteProgram(_id);}

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        // Movable: nothing outside the object holds its address.
        Shader(Shader&& other) noexcept
            : _id(std::exchange(other._id, 0)), _isCompute(other._isCompute),
              _locations(std::move(other._locations)) {}

        Shader& operator=(Shader&& other) noexcept {
            if (this != &other) {
                if (_id) glDeleteProgram(_id);
                _id = std::exchange(other._id, 0);
                _isCompute = other._isCompute;
                _locations = std::move(other._locations);
            }
            return *this;
        }
    private:
        Shader(unsigned int id, bool isCompute) noexcept : _id(id), _isCompute(isCompute) {}

        #if ENGINE_HAS_PROGRAM_RESOURCE
            // Interfaces whose resources have a location, per the GL 4.3 spec.
            static bool interfaceHasLocation(unsigned int programInterface) noexcept {
                switch (programInterface) {
                    case GL_UNIFORM:
                    case GL_PROGRAM_INPUT:
                    case GL_PROGRAM_OUTPUT:
                    case GL_VERTEX_SUBROUTINE_UNIFORM:
                    case GL_FRAGMENT_SUBROUTINE_UNIFORM:
                    case GL_GEOMETRY_SUBROUTINE_UNIFORM:
                    case GL_TESS_CONTROL_SUBROUTINE_UNIFORM:
                    case GL_TESS_EVALUATION_SUBROUTINE_UNIFORM:
                    case GL_COMPUTE_SUBROUTINE_UNIFORM:
                        return true;
                    default:
                        return false;
                }
            }

            // Interfaces whose resources carry GL_TYPE and GL_ARRAY_SIZE.
            static bool interfaceHasType(unsigned int programInterface) noexcept {
                switch (programInterface) {
                    case GL_UNIFORM:
                    case GL_PROGRAM_INPUT:
                    case GL_PROGRAM_OUTPUT:
                    case GL_BUFFER_VARIABLE:
                    case GL_TRANSFORM_FEEDBACK_VARYING:
                        return true;
                    default:
                        return false;
                }
            }
        #endif

        // True when the config describes a compute program.
        static bool isComputeConfig(const ShaderConfig& config) noexcept {
            #if ENGINE_HAS_COMPUTE_SHADER
                return !config.compute.empty();
            #else
                (void)config;
                return false;
            #endif
        }

        // Compiles every configured stage into `modules`; false on any failure.
        static bool compileStages(const ShaderConfig& config, std::vector<unsigned int>& modules) noexcept {
            const auto attach = [&](const std::vector<ShaderSource>& sources, unsigned int type) {
                for (const ShaderSource& source : sources) {
                    const unsigned int module = makeModule(source, type, config.defines);
                    if (!module) return false;
                    modules.push_back(module);
                }
                return true;
            };

            #if ENGINE_HAS_COMPUTE_SHADER
                if (!config.compute.empty()) {
                    if (!config.vertex.empty() || !config.fragment.empty()) {
                        log_error("A compute shader cannot be linked with other stages");
                        return false;
                    }
                    return attach(config.compute, GL_COMPUTE_SHADER);
                }
            #endif

            if (config.vertex.empty() || config.fragment.empty()) {
                log_error("A graphics program needs at least one vertex and one fragment shader");
                return false;
            }
            if (!attach(config.vertex, GL_VERTEX_SHADER)) return false;
            if (!attach(config.fragment, GL_FRAGMENT_SHADER)) return false;
            #if ENGINE_HAS_GEOMETRY_SHADER
                if (!attach(config.geometry, GL_GEOMETRY_SHADER)) return false;
            #endif
            #if ENGINE_HAS_TESS_SHADER
                if (!attach(config.tess_control, GL_TESS_CONTROL_SHADER)) return false;
                if (!attach(config.tess_evaluation, GL_TESS_EVALUATION_SHADER)) return false;
            #endif
            return true;
        }

        // Loads a source's text, from memory or from its file.
        static std::string readSource(const ShaderSource& source) noexcept {
            if (!source.text.empty()) return source.text;

            std::ifstream file(source.file);
            if (!file.is_open()) {
                std::error_code ec;
                const std::filesystem::path cwd = std::filesystem::current_path(ec);
                log_error("Failed to open shader file: ", source.file,
                          " (working directory: ", ec ? std::string("unavailable") : cwd.string(), ")");
                return "";
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        // Inserts #define lines after the #version directive, fixing up #line.
        static std::string injectDefines(const std::string& source,
                                         const std::vector<std::string>& defines) noexcept {
            if (defines.empty()) return source;

            size_t insertAt = 0;
            const size_t versionPos = source.find("#version");
            if (versionPos != std::string::npos) {
                const size_t lineEnd = source.find('\n', versionPos);
                insertAt = (lineEnd == std::string::npos) ? source.size() : lineEnd + 1;
            }

            // #line sets the number of the NEXT line. Inserting after a #version
            // directive resumes at the original line 2, not line 1.
            std::string block;
            for (const std::string& define : defines) block += "#define " + define + "\n";
            block += (versionPos != std::string::npos) ? "#line 2\n" : "#line 1\n";
            return source.substr(0, insertAt) + block + source.substr(insertAt);
        }

        // Compiles one shader module; returns 0 and logs on failure.
        static unsigned int makeModule(const ShaderSource& source, unsigned int type,
                                       const std::vector<std::string>& defines) noexcept {
            const std::string raw = readSource(source);
            if (raw.empty()) return 0;

            const std::string text = injectDefines(raw, defines);
            const char* pointer = text.c_str();

            const unsigned int module = glCreateShader(type);
            glShaderSource(module, 1, &pointer, nullptr);
            glCompileShader(module);

            int success = 0;
            glGetShaderiv(module, GL_COMPILE_STATUS, &success);
            if (!success) {
                log_error("Shader compilation error in ", source.name, ":\n", shaderLog(module));
                glDeleteShader(module);
                return 0;
            }
            return module;
        }

        // The compile info log of a shader module. GL_INFO_LOG_LENGTH counts the
        // terminator, so the string is one shorter or it ends in an embedded NUL.
        static std::string shaderLog(unsigned int module) noexcept {
            int length = 0;
            glGetShaderiv(module, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return "(no log)";
            std::string log(static_cast<size_t>(length - 1), '\0');
            glGetShaderInfoLog(module, length, nullptr, log.data());
            return log;
        }

        // The link/validate info log of a program. Sized as shaderLog above.
        static std::string programLog(unsigned int program) noexcept {
            int length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            if (length <= 1) return "(no log)";
            std::string log(static_cast<size_t>(length - 1), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            return log;
        }

        // Enumerates active attributes or uniforms via the pre-4.3 query API.
        std::vector<ShaderResource> activeResources(unsigned int countEnum, unsigned int lengthEnum,
                                                    bool isAttribute) const noexcept {
            std::vector<ShaderResource> out;
            int count = 0;
            glGetProgramiv(_id, static_cast<GLenum>(countEnum), &count);
            if (count <= 0) return out;

            int maxLength = 0;
            glGetProgramiv(_id, static_cast<GLenum>(lengthEnum), &maxLength);
            if (maxLength <= 0) maxLength = 256;

            std::string buffer(static_cast<size_t>(maxLength), '\0');
            for (int i = 0; i < count; ++i) {
                int length = 0, size = 0;
                GLenum type = 0;
                if (isAttribute) {
                    glGetActiveAttrib(_id, static_cast<unsigned int>(i), maxLength,
                                      &length, &size, &type, buffer.data());
                } else {
                    glGetActiveUniform(_id, static_cast<unsigned int>(i), maxLength,
                                       &length, &size, &type, buffer.data());
                }
                ShaderResource resource;
                resource.name.assign(buffer.data(), static_cast<size_t>(length));
                resource.type = type;
                resource.size = size;
                resource.location = isAttribute ? glGetAttribLocation(_id, resource.name.c_str())
                                                : glGetUniformLocation(_id, resource.name.c_str());
                out.push_back(std::move(resource));
            }
            return out;
        }

        // Assigns sampler uniforms to texture units in list order.
        void bindSamplers(const std::vector<std::string>& names) const noexcept {
            if (names.empty()) return;
            #if ENGINE_HAS_PROGRAM_UNIFORM
                for (size_t unit = 0; unit < names.size(); ++unit) {
                    const int location = uniformLocation(names[unit]);
                    if (location != -1) glProgramUniform1i(_id, location, static_cast<int>(unit));
                }
            #else
                int previous = 0;
                glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
                use();
                for (size_t unit = 0; unit < names.size(); ++unit) {
                    const int location = uniformLocation(names[unit]);
                    if (location != -1) glUniform1i(location, static_cast<int>(unit));
                }
                glUseProgram(static_cast<unsigned int>(previous));
            #endif
        }

        unsigned int _id = 0;
        bool _isCompute = false;
        mutable std::unordered_map<std::string, int> _locations;
};

#if ENGINE_HAS_SEPARATE_PROGRAM
class ShaderPipeline {
    public:
        // Creates an empty program pipeline; returns null on failure.
        static std::unique_ptr<ShaderPipeline> create() noexcept {
            unsigned int id = 0;
            glGenProgramPipelines(1, &id);
            if (!id) {log_error("Failed to create program pipeline"); return nullptr;}
            return std::unique_ptr<ShaderPipeline>(new ShaderPipeline(id));
        }

        // The GL pipeline name.
        unsigned int id() const noexcept {return _id;}
        // Binds this pipeline.
        void bind() const noexcept {glBindProgramPipeline(_id);}
        // Unbinds any pipeline.
        static void unbind() noexcept {glBindProgramPipeline(0);}

        // Routes the given stage bits to a separable program.
        void useStages(GLbitfield stages, const Shader& shader) const noexcept {
            glUseProgramStages(_id, stages, shader.id());
        }

        // Clears the given stage bits.
        void clearStages(GLbitfield stages) const noexcept {glUseProgramStages(_id, stages, 0);}

        // Runs glValidateProgramPipeline and logs the info log on failure.
        bool validate() const noexcept {
            glValidateProgramPipeline(_id);
            int success = 0;
            glGetProgramPipelineiv(_id, GL_VALIDATE_STATUS, &success);
            if (!success) {
                int length = 0;
                glGetProgramPipelineiv(_id, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
                if (length > 0) glGetProgramPipelineInfoLog(_id, length, nullptr, log.data());
                log_error("Pipeline validation failed:\n", length > 0 ? log : std::string("(no log)"));
            }
            return success != 0;
        }

        ~ShaderPipeline() noexcept {if (_id) glDeleteProgramPipelines(1, &_id);}

        ShaderPipeline(const ShaderPipeline&) = delete;
        ShaderPipeline& operator=(const ShaderPipeline&) = delete;
        ShaderPipeline(ShaderPipeline&& other) noexcept
            : _id(std::exchange(other._id, 0)) {}

        ShaderPipeline& operator=(ShaderPipeline&& other) noexcept {
            if (this != &other) {
                if (_id) glDeleteProgramPipelines(1, &_id);
                _id = std::exchange(other._id, 0);
            }
            return *this;
        }
    private:
        explicit ShaderPipeline(unsigned int id) noexcept : _id(id) {}
        unsigned int _id = 0;
};
#endif

namespace shader_lib {

    // ---- reusable GLSL snippets ----

    // Rotates a vector by a unit quaternion without building a matrix. Wanted by
    // any shader that takes a rotation as an attribute rather than a uniform.
    inline std::string quaternion_rotate() noexcept {return shader_tools::quaternionRotateGLSL();}

    // sRGB and linear conversion. Author colours in sRGB, shade in linear, and
    // convert on the way out; a physically based model needs linear input.
    inline std::string color_space() noexcept {
        return shader_tools::glsl(R"GLSL(

            vec3 srgbToLinear(vec3 c) {return pow(max(c, vec3(0.0)), vec3(2.2));}
            vec3 linearToSrgb(vec3 c) {return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));}
            )GLSL");
    }

    // ---- grid ----

    // Vertex stage for the world-space grid: a plain transform with a height offset.
    inline ShaderSource grid_vert() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            layout(location = 0) in vec3 aPosition;
            uniform mat4 uViewProjection;
            uniform float uHeight;
            void main() {gl_Position = uViewProjection * vec4(aPosition + vec3(0.0, uHeight, 0.0), 1.0);}
            )GLSL"), "grid.vert");
    }

    // Fragment stage for the world-space grid: one flat colour.
    inline ShaderSource grid_frag() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            uniform vec4 uColor;
            out vec4 FragColor;
            void main() {FragColor = uColor;}
            )GLSL"), "grid.frag");
    }

    // Builds the grid program; null when compilation or linking fails.
    inline std::unique_ptr<Shader> grid_shader() noexcept {
        ShaderConfig config;
        config.vertex.emplace_back(grid_vert());
        config.fragment.emplace_back(grid_frag());
        return Shader::create(config);
    }

    // ---- sprite quads ----

    // Matches Renderer2D::quadLayout(): position, UV, packed RGBA8 tint. The
    // generated 2D lighting shader cannot draw these -- it declares no sampler
    // and no tint attribute -- so the quad path needs its own program.
    inline ShaderSource quad_vert() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            layout(location = 0) in vec2 aPosition;
            layout(location = 1) in vec2 aUV;
            layout(location = 2) in vec4 aTint;

            uniform mat4 uModel;
            uniform mat4 uViewProjection;

            out vec2 vUV;
            out vec4 vTint;

            void main() {
                vUV = aUV;
                vTint = aTint;
                gl_Position = uViewProjection * uModel * vec4(aPosition, 0.0, 1.0);
            }
            )GLSL"), "quad.vert");
    }

    // Fragment stage for sprite quads: texture times tint, fully transparent
    // fragments discarded so overlapping sprites need no depth sorting.
    inline ShaderSource quad_frag() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            in vec2 vUV;
            in vec4 vTint;

            uniform sampler2D uTexture;

            out vec4 FragColor;

            void main() {
                vec4 texel = texture(uTexture, vUV);
                if (texel.a * vTint.a < 0.001) discard;
                FragColor = texel * vTint;
            }
            )GLSL"), "quad.frag");
    }

    // Builds the sprite-quad program, with uTexture bound to unit 0.
    inline std::unique_ptr<Shader> quad_shader() noexcept {
        ShaderConfig config;
        config.vertex.emplace_back(quad_vert());
        config.fragment.emplace_back(quad_frag());
        config.sampler_names = {"uTexture"};
        return Shader::create(config);
    }

    // ---- shadow depth ----

    // Depth-only pass for filling a shadow map. Reads attribute location 0 only,
    // so any vertex layout that starts with a position works, and writes no
    // colour. LightingPipeline3D builds this itself today; moving it here lets a
    // custom depth pass reuse the exact same program.
    inline ShaderSource shadow_depth_vert() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            layout(location = 0) in vec3 aPosition;
            uniform mat4 uModel;
            uniform mat4 uLightViewProjection;
            void main() {gl_Position = uLightViewProjection * uModel * vec4(aPosition, 1.0);}
            )GLSL"), "shadow_depth.vert");
    }

    // Instanced counterpart, matching the aInstancePosScale / aInstanceRotation
    // attributes at locations 3 and 4 that Renderer3D binds.
    inline ShaderSource shadow_depth_instanced_vert() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + quaternion_rotate()
                                      + shader_tools::glsl(R"GLSL(

            layout(location = 0) in vec3 aPosition;
            layout(location = 3) in vec4 aInstancePosScale;
            layout(location = 4) in vec4 aInstanceRotation;

            uniform mat4 uLightViewProjection;

            void main() {
                vec3 world = rotateByQuaternion(aInstanceRotation, aPosition * aInstancePosScale.w)
                           + aInstancePosScale.xyz;
                gl_Position = uLightViewProjection * vec4(world, 1.0);
            }
            )GLSL"), "shadow_depth_instanced.vert");
    }

    // Fragment stage for the depth pass: writes nothing but the implicit depth.
    inline ShaderSource shadow_depth_frag() noexcept {
        return ShaderSource::fromText(Platform::get_glsl_version_str() + shader_tools::glsl(R"GLSL(

            void main() {}
            )GLSL"), "shadow_depth.frag");
    }

    // Builds the depth-only program, instanced or per-object.
    inline std::unique_ptr<Shader> shadow_depth_shader(bool instanced = false) noexcept {
        ShaderConfig config;
        config.vertex.emplace_back(instanced ? shadow_depth_instanced_vert() : shadow_depth_vert());
        config.fragment.emplace_back(shadow_depth_frag());
        return Shader::create(config);
    }
}