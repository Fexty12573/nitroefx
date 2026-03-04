#pragma once

#include <GL/glew.h>
#include <spdlog/spdlog.h>

#include <map>
#include <fstream>
#include <filesystem>

#include "gfx/gl_util.h"
#include <types.h>


class GLShader {
public:
    GLShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    GLShader(std::string_view vertexSource, std::string_view fragmentSource);
    ~GLShader();

    GLShader(const GLShader&) = delete;
    GLShader(GLShader&&) = delete;
    GLShader& operator=(const GLShader&) = delete;
    GLShader& operator=(GLShader&&) = delete;

    void bind() const;
    void unbind() const;

    u32 getUniform(const std::string& name);

private:
    void compileShader(std::string_view vertexSource, std::string_view fragmentSource);

private:
    u32 m_shader = 0;
    std::map<std::string, u32> m_uniformCache;
};
