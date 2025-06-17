#include <random>
#include <wayfire/geometry.hpp>
#include <wayfire/toplevel.hpp>
#include "renderer.hpp"

namespace winshadows {

shadow_renderer_t::shadow_renderer_t() {
        wf::gles::run_in_context([&]
        {
    generate_dither_texture();
    recompile_shaders();
    });

    light_type_option.set_callback([this] () {
        recompile_shaders();
    });
}

void shadow_renderer_t::recompile_shaders() {
        wf::gles::run_in_context([&]
        {
    shadow_program.free_resources();
    shadow_glow_program.free_resources();

    shadow_program.set_simple(
        OpenGL::compile_program(shadow_vert_shader, frag_shader(light_type_option, /*no glow*/ false))
    );
    shadow_glow_program.set_simple(
        OpenGL::compile_program(shadow_vert_shader, frag_shader(light_type_option, /*glow*/ true))
    );

    });
}

void shadow_renderer_t::generate_dither_texture() {
    const int size = 32;
    GLuint data[size*size];

    std::mt19937_64 gen{std::random_device{}()};
    std::uniform_int_distribution<GLuint> distrib;

    for (int i = 0; i < size*size; i++) {
        data[i] = distrib(gen);
    }

    GL_CALL(glGenTextures(1, &dither_texture));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, dither_texture));
    GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));
}

shadow_renderer_t::~shadow_renderer_t() {
        wf::gles::run_in_context([&]
        {
    shadow_program.free_resources();
    shadow_glow_program.free_resources();

    GL_CALL(glDeleteTextures(1, &dither_texture));

    });
}

void shadow_renderer_t::render(const wf::scene::render_instruction_t& data, wf::point_t window_origin, const wf::geometry_t& scissor, const bool glow) {
    float radius = shadow_radius_option;

    wf::color_t color = shadow_color_option;

    // Premultiply alpha for shader
    glm::vec4 premultiplied = {
        color.r * color.a,
        color.g * color.a,
        color.b * color.a,
        color.a
    };

    // Glow color, alpha=0 => additive blending (exploiting premultiplied alpha)
    wf::color_t glow_color = glow_color_option;
    glm::vec4 glow_premultiplied = {
        glow_color.r * glow_color.a,
        glow_color.g * glow_color.a,
        glow_color.b * glow_color.a,
        glow_color.a * (1.0 - glow_emissivity_option)
    };

    // Enable glow shader only when glow radius > 0 and view is focused
    bool use_glow = (glow && is_glow_enabled());
    OpenGL::program_t &program = 
        use_glow ? shadow_glow_program : shadow_program;

            data.pass->custom_gles_subpass(data.target,[&]
            {

                wf::gles::render_target_logic_scissor(data.target, scissor);
    program.use(wf::TEXTURE_TYPE_RGBA);

    // Compute vertex rectangle geometry
    wf::geometry_t bounds = outer_geometry + window_origin;
    float left = bounds.x;
    float right = bounds.x + bounds.width;
    float top = bounds.y;
    float bottom = bounds.y + bounds.height;

    GLfloat vertexData[] = {
        left, bottom,
        right, bottom,
        right, top,
        left, top
    };

    glm::mat4 matrix = wf::gles::render_target_orthographic_projection(data.target);

    // vertex parameters
    program.attrib_pointer("position", 2, 0, vertexData);
    program.uniformMatrix4f("MVP", matrix);

    // fragment parameters
    program.uniform1f("radius", radius);
    program.uniform4f("color", premultiplied);

    const auto inner = window_geometry + window_origin;
    const auto shadow_inner = shadow_projection_geometry + window_origin;
    program.uniform2f("lower", shadow_inner.x, shadow_inner.y);
    program.uniform2f("upper", shadow_inner.x + shadow_inner.width, shadow_inner.y + shadow_inner.height);

    if (use_glow) {
        program.uniform2f("glow_lower", inner.x, inner.y);
        program.uniform2f("glow_upper", inner.x + inner.width, inner.y + inner.height);

        program.uniform1f("glow_spread", glow_spread_option);
        program.uniform4f("glow_color", glow_premultiplied);
        program.uniform1f("glow_intensity",  glow_intensity_option);
        program.uniform1f("glow_threshold",  glow_threshold_option);
    }

    // dither texture
    program.uniform1i("dither_texture", 0);
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, dither_texture));

    GL_CALL(glEnable(GL_BLEND));
    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    program.deactivate();
    });
}

wf::region_t shadow_renderer_t::calculate_region() const {
    // TODO: geometry and region depending on whether glow is active or not
    wf::region_t region = wf::region_t(shadow_geometry) | wf::region_t(glow_geometry);

    if (clip_shadow_inside) {
        region ^= window_geometry;
    }

    return region;
}

wf::geometry_t shadow_renderer_t::get_geometry() const {
    return outer_geometry;
}

wf::geometry_t expand_geometry(const wf::geometry_t& geometry, const int marginX, const int marginY) {
    return {
        geometry.x - marginX,
        geometry.y - marginY,
        geometry.width + marginX * 2,
        geometry.height + marginY * 2
    };
}

wf::geometry_t expand_geometry(const wf::geometry_t& geometry, const int margin) {
    return expand_geometry(geometry, margin, margin);
}

wf::geometry_t inflate_geometry(const wf::geometry_t& geometry, const float inflation) {
    int expandX = geometry.width * inflation * 0.5;
    int expandY = geometry.height * inflation * 0.5;
    return expand_geometry(geometry, expandX, expandY);
}

void shadow_renderer_t::resize(const int window_width, const int window_height) {
    window_geometry = {
        0,
        0,
        window_width,
        window_height
    };

    float overscale = overscale_option / 100.0;
    const wf::point_t offset { horizontal_offset, vertical_offset };
    shadow_projection_geometry =
        inflate_geometry(window_geometry, overscale) + offset;

    shadow_geometry = expand_geometry(shadow_projection_geometry, shadow_radius_option);

    int glow_radius = is_glow_enabled() ? glow_radius_limit_option : 0;
    glow_geometry = expand_geometry(shadow_projection_geometry, glow_radius);

    int left = std::min(shadow_geometry.x, glow_geometry.x);
    int top = std::min(shadow_geometry.y, glow_geometry.y);
    int right = std::max(shadow_geometry.x + shadow_geometry.width, glow_geometry.x + glow_geometry.width);
    int bottom = std::max(shadow_geometry.y + shadow_geometry.height, glow_geometry.y + glow_geometry.height);
    outer_geometry = {
        left,
        top,
        right - left,
        bottom - top
    };
}

bool shadow_renderer_t::is_glow_enabled() const {
    return glow_enabled_option && (glow_radius_limit_option > 0) && (glow_intensity_option > 0);
}

}
