#include "node.hpp"

namespace winshadows {

shadow_node_t::shadow_node_t( wayfire_toplevel_view view ): wf::scene::node_t(false) {
    this->view = view;
    on_geometry_changed.set_callback([this] (auto) {
        update_geometry();
    });
    on_activated_changed.set_callback([this] (auto) {
        this->view->damage();
    });
    view->connect(&on_geometry_changed);
    view->connect(&on_activated_changed);
    update_geometry();
}

shadow_node_t::~shadow_node_t() {
    view->disconnect(&on_geometry_changed);
}

wf::geometry_t shadow_node_t::get_bounding_box()  {
    return geometry;
}

void shadow_node_t::gen_render_instances(std::vector<wf::scene::render_instance_uptr> &instances, wf::scene::damage_callback push_damage, wf::output_t *output) {
    // define renderer
    class shadow_render_instance_t : public wf::scene::simple_render_instance_t<shadow_node_t> {
      public:
        using simple_render_instance_t::simple_render_instance_t;
        void render(const wf::scene::render_instruction_t& data) override
        {
            // coordinates relative to view origin (not bounding box origin)
            wf::point_t frame_origin = self->frame_offset;

            for (const auto& box : data.damage)

            {
                self->shadow.render(data, frame_origin, wlr_box_from_pixman_box(box) , self->view->activated);
            }
            self->_was_activated = self->view->activated;
        }
    };

    instances.push_back(std::make_unique<shadow_render_instance_t>(this, push_damage, output));
}

void shadow_node_t::update_geometry() {
    wf::geometry_t frame_geometry = view->get_geometry();
    shadow.resize(frame_geometry.width, frame_geometry.height);

    // TODO: Check whether this can be done in a nicer/easier way
    wf::pointf_t view_origin_f = view->get_surface_root_node()->to_global({0, 0}); 
    wf::point_t view_origin {(int)view_origin_f.x, (int)view_origin_f.y};

    // Offset between view origin and frame top left corner
    frame_offset = wf::origin(frame_geometry) - view_origin;

    // Shadow geometry is relative to the top left corner of the frame (not the view)
    wf::geometry_t shadow_geometry = shadow.get_geometry();

    // move to view-relative coordinates
    geometry = shadow_geometry + frame_offset;

    this->shadow_region = shadow.calculate_region();
}

}
