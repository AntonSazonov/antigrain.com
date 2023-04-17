#include <stdio.h>
#include "agg_basics.h"
#include "agg_rendering_buffer.h"
#include "agg_scanline_u.h"
#include "agg_scanline_bin.h"
#include "agg_renderer_scanline.h"
#include "agg_renderer_primitives.h"
#include "agg_rasterizer_scanline_aa.h"
#include "agg_conv_curve.h"
#include "agg_conv_contour.h"
#include "agg_pixfmt_rgb.h"
#include "agg_gamma_lut.h"
#include "agg_font_win32_tt.h"
#include "platform/agg_platform_support.h"

#include "ctrl/agg_slider_ctrl.h"
#include "ctrl/agg_cbox_ctrl.h"
#include "ctrl/agg_rbox_ctrl.h"


enum { flip = true };
bool text_flip = false;

class the_application : public agg::platform_support
{
    typedef agg::gamma_lut<agg::int8u, agg::int16u, 8, 16> gamma_type;
    typedef agg::pixfmt_bgr24_gamma<gamma_type> pixfmt_type;
    typedef agg::renderer_base<pixfmt_type> base_ren_type;
    typedef agg::renderer_scanline_aa_solid<base_ren_type> renderer_solid;
    typedef agg::renderer_scanline_bin_solid<base_ren_type> renderer_bin;
    typedef agg::font_engine_win32_tt_int32 font_engine_type;
    typedef agg::font_cache_manager<font_engine_type> font_manager_type;

    agg::rbox_ctrl<agg::rgba8>   m_ren_type;
    agg::slider_ctrl<agg::rgba8> m_height;
    agg::slider_ctrl<agg::rgba8> m_width;
    agg::slider_ctrl<agg::rgba8> m_weight;
    agg::slider_ctrl<agg::rgba8> m_gamma;
    agg::cbox_ctrl<agg::rgba8>   m_hinting;
    agg::cbox_ctrl<agg::rgba8>   m_kerning;
    agg::cbox_ctrl<agg::rgba8>   m_performance;
    font_engine_type             m_feng;
    font_manager_type            m_fman;
    double                       m_old_height;
    gamma_type                   m_gamma_lut;

    // Pipeline to process the vectors glyph paths (curves + contour)
    agg::conv_curve<font_manager_type::path_adaptor_type> m_curves;
    agg::conv_contour<agg::conv_curve<font_manager_type::path_adaptor_type> > m_contour;

public:
    the_application(HDC dc, agg::pix_format_e format, bool flip) :
        agg::platform_support(format, flip),
        m_ren_type     (5.0, 5.0, 5.0+150.0,   110.0,  !flip),
        m_height       (160, 10.0, 640-5.0,    18.0,   !flip),
        m_width        (160, 30.0, 640-5.0,    38.0,   !flip),
        m_weight       (160, 50.0, 640-5.0,    58.0,   !flip),
        m_gamma        (260, 70.0, 640-5.0,    78.0,   !flip),
        m_hinting      (160, 65.0, "Hinting", !flip),
        m_kerning      (160, 80.0, "Kerning", !flip),
        m_performance  (160, 95.0, "Test Performance", !flip),
        m_feng(dc),
        m_fman(m_feng),
        m_old_height(0.0),
        m_curves(m_fman.path_adaptor()),
        m_contour(m_curves)
    {
        m_ren_type.add_item("Native Mono");
        m_ren_type.add_item("Native Gray 8");
        m_ren_type.add_item("Outline");
        m_ren_type.add_item("AGG Mono");
        m_ren_type.add_item("AGG Gray 8");
        m_ren_type.cur_item(1);
        add_ctrl(m_ren_type);
        m_ren_type.no_transform();

        m_height.label("Font Height=%.2f");
        m_height.range(8, 32);
        m_height.num_steps(32-8);
        m_height.value(24);
        m_height.text_thickness(1.5);
        add_ctrl(m_height);
        m_height.no_transform();

        m_width.label("Font Width=%.2f");
        m_width.range(8, 32);
        m_width.num_steps(32-8);
        m_width.text_thickness(1.5);
        m_width.value(18);
        add_ctrl(m_width);
        m_width.no_transform();

        m_weight.label("Font Weight=%.2f");
        m_weight.range(-1, 1);
        m_weight.text_thickness(1.5);
        add_ctrl(m_weight);
        m_weight.no_transform();

        m_gamma.label("Gamma=%.2f");
        m_gamma.range(0.1, 2.0);
        m_gamma.value(1.0);
        m_gamma.text_thickness(1.5);
        add_ctrl(m_gamma);
        m_gamma.no_transform();

        add_ctrl(m_hinting);
        m_hinting.status(true);
        m_hinting.no_transform();

        add_ctrl(m_kerning);
        m_kerning.status(true);
        m_kerning.no_transform();

        add_ctrl(m_performance);
        m_performance.no_transform();

        m_curves.approximation_scale(2.0);
        m_contour.auto_detect_orientation(false);
    }


    template<class Rasterizer, class Scanline, class RenSolid, class RenBin>
    unsigned draw_text(Rasterizer& ras, Scanline& sl, 
                   RenSolid& ren_solid, RenBin& ren_bin)
    {
        agg::glyph_rendering gren = agg::glyph_ren_native_mono;
        switch(m_ren_type.cur_item())
        {
        case 0: gren = agg::glyph_ren_native_mono;  break;
        case 1: gren = agg::glyph_ren_native_gray8; break;
        case 2: gren = agg::glyph_ren_outline;      break;
        case 3: gren = agg::glyph_ren_agg_mono;     break;
        case 4: gren = agg::glyph_ren_agg_gray8;    break;
        }

        unsigned num_glyphs = 0;

        m_contour.width(-m_weight.value() * m_height.value() * 0.05);

        m_feng.hinting(m_hinting.status());
        m_feng.height(m_height.value());

        // Font width in Windows is strange. MSDN says, 
        // "specifies the average width", but there's no clue what
        // this "average width" means. It'd be logical to specify 
        // the width with regard to the font height, like it's done in 
        // FreeType. That is, width == height should mean the "natural", 
        // not distorted glyphs. In Windows you have to specify
        // the absolute width, which is very stupid and hard to use 
        // in practice.
        //-------------------------
        m_feng.width((m_width.value() == m_height.value()) ? 0.0 : m_width.value() / 2.4);
//        m_feng.italic(true);
        m_feng.flip_y(text_flip);
        m_feng.char_set(DEFAULT_CHARSET);

        if(m_feng.create_font("KaiTi_GB2312", gren))
        //if(m_feng.create_font("SimSun", gren))
        {
//            m_fman.precache(' ', 127);

            double x = 10.0;
            double y0 = height() - m_height.value() - 10.0;
            double y = y0;

            //unsigned text[] = { 0xd6d0, 0xB9Fa, 0x28, 0x73, 0x69, 0x6D, 0x6B, 0x61, 0x69, 0x2E, 0x74, 0x74, 0x66, 0x29, 0 };

            unsigned start_char = 30000;
            unsigned num_chars  = 1000;

            while(num_chars)
            {
                const agg::glyph_cache* glyph = m_fman.glyph(start_char);
                if(glyph)
                {
                    if(m_kerning.status())
                    {
                        m_fman.add_kerning(&x, &y);
                    }

                    if(x >= width() - m_height.value())
                    {
                        x = 10.0;
                        y0 -= m_height.value();
                        if(y0 <= 120) break;
                        y = y0;
                    }

                    m_fman.init_embedded_adaptors(glyph, x, y);

                    switch(glyph->data_type)
                    {
                    case agg::glyph_data_mono:
                        ren_bin.color(agg::rgba8(0, 0, 0));
                        agg::render_scanlines(m_fman.mono_adaptor(), 
                                              m_fman.mono_scanline(), 
                                              ren_bin);
                        break;

                    case agg::glyph_data_gray8:
                        ren_solid.color(agg::rgba8(0, 0, 0));
                        agg::render_scanlines(m_fman.gray8_adaptor(), 
                                              m_fman.gray8_scanline(), 
                                              ren_solid);
                        break;

                    case agg::glyph_data_outline:
                        ras.reset();
                        if(fabs(m_weight.value()) <= 0.01)
                        {
                            // For the sake of efficiency skip the
                            // contour converter if the weight is about zero.
                            //-----------------------
                            ras.add_path(m_curves);
                        }
                        else
                        {
                            ras.add_path(m_contour);
                        }
                        ren_solid.color(agg::rgba8(0, 0, 0));
                        agg::render_scanlines(ras, sl, ren_solid);
                        break;
                    }

                    // increment pen position
                    x += glyph->advance_x;
                    y += glyph->advance_y;
                    ++num_glyphs;
                }
                ++start_char;
            }
        }

        return num_glyphs;
    }


    virtual void on_draw()
    {
        pixfmt_type pf(rbuf_window(), m_gamma_lut);
        base_ren_type ren_base(pf);
        renderer_solid ren_solid(ren_base);
        renderer_bin ren_bin(ren_base);
        ren_base.clear(agg::rgba(1,1,1));

        agg::scanline_u8 sl;
        agg::rasterizer_scanline_aa<> ras;

        if(m_height.value() != m_old_height)
        {
            m_width.value(m_old_height = m_height.value());
        }

        if(m_ren_type.cur_item() == 3)
        {
            // When rendering in mono format, 
            // Set threshold gamma = 0.5
            //-------------------
            m_feng.gamma(agg::gamma_threshold(m_gamma.value() / 2.0));
        }
        else
        {
            m_feng.gamma(agg::gamma_none());
            m_gamma_lut.gamma(m_gamma.value());
        }


        draw_text(ras, sl, ren_solid, ren_bin);

        ras.gamma(agg::gamma_power(1.0));


        agg::render_ctrl(ras, sl, ren_solid, m_ren_type);
        agg::render_ctrl(ras, sl, ren_solid, m_height);
        agg::render_ctrl(ras, sl, ren_solid, m_width);
        agg::render_ctrl(ras, sl, ren_solid, m_weight);
        agg::render_ctrl(ras, sl, ren_solid, m_gamma);
        agg::render_ctrl(ras, sl, ren_solid, m_hinting);
        agg::render_ctrl(ras, sl, ren_solid, m_kerning);
        agg::render_ctrl(ras, sl, ren_solid, m_performance);
    }


    

    virtual void on_ctrl_change()
    {
        if(m_performance.status())
        {
            pixfmt_type pf(rbuf_window(), m_gamma_lut);
            base_ren_type ren_base(pf);
            renderer_solid ren_solid(ren_base);
            renderer_bin ren_bin(ren_base);
            ren_base.clear(agg::rgba(1,1,1));

            agg::scanline_u8 sl;
            agg::rasterizer_scanline_aa<> ras;

            unsigned num_glyphs = 0;
            start_timer();
            for(int i = 0; i < 50; i++)
            {
                num_glyphs += draw_text(ras, sl, ren_solid, ren_bin);
            }
            double t = elapsed_time();
            char buf[100];
            sprintf(buf, 
                    "Glyphs=%u, Time=%.3fms, %.3f glyps/sec, %.3f microsecond/glyph", 
                    num_glyphs,
                    t, 
                    (num_glyphs / t) * 1000.0, 
                    (t / num_glyphs) * 1000.0);
            message(buf);

            m_performance.status(false);
            force_redraw();
        }
    }

    virtual void on_key(int x, int y, unsigned key, unsigned flags)
    {
        text_flip = !text_flip;
        force_redraw();
    }


};



int agg_main(int argc, char* argv[])
{
    HDC dc = ::GetDC(0);
    the_application app(dc, agg::pix_format_bgr24, flip);
    app.caption("AGG Example. Rendering TrueType Fonts with WinAPI");

    if(app.init(640, 520, agg::window_resize))
    {
        return app.run();
    }
    ::ReleaseDC(0, dc);
    return 1;
}


