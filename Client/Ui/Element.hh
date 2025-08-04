#pragma once

#include <Client/Render/Renderer.hh>
#include <Shared/Helpers.hh>

#include <functional>
#include <vector>

namespace Ui {
    class Element;

    struct Style {
        enum {
            Top = -1,
            Middle = 0,
            Bottom = 1,
            Left = -1,
            Center = 0,
            Right = 1
        };
        uint32_t fill = 0x00000000;
        float stroke_hsv = 0.8;
        float line_width = 3;
        float round_radius = 0;
        std::function<void(Element *, Renderer &)> animate = nullptr;
        std::function<bool(void)> should_render = nullptr;
        int8_t h_justify = Center;
        int8_t v_justify = Middle;
        uint8_t layer = 0;
        uint8_t no_animation = 0;
        uint8_t no_polling = 0;
    };

    enum UiEvent {
        kFocusLost,
        kMouseDown,
        kClick,
        kMouseUp,
        kMouseMove,
        kMouseHover
    };

    class Element {
    protected:
        std::vector<Element *> children;
        Ui::Element *tooltip = nullptr;
    public:
        Ui::Element *parent = nullptr;
        float width = 0;
        float height = 0;
        float x = 0;
        float y = 0;
        float screen_x = 0;
        float screen_y = 0;
        LerpFloat animation;
        LerpFloat tooltip_animation;
        Ui::Style style;

        uint8_t focus_state : 4 = 0;
        uint8_t visible : 1 = 1;
        uint8_t showed : 1 = 0;
        uint8_t rendering_tooltip : 1 = 0;

        Element(float = 0, float = 0, Style = {});
        void add_child(Element *);
        void render(Renderer &);
        virtual void on_render(Renderer &);
        virtual void on_render_tooltip(Renderer &);
        virtual void on_render_skip(Renderer &);
        virtual void on_event(uint8_t);
        //std::function<void(Renderer &)> animate;
        virtual void refactor();
        virtual void poll_events();
    };

    std::vector<Element *> const make_range(uint32_t, uint32_t, Element *(uint32_t));
}