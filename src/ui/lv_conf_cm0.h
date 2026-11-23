#pragma once
// Configuración LVGL para el Cardputer Zero (basada en la de ZeroPower), pero "lean":
// sólo framebuffer + evdev + fuentes integradas. SIN freetype/png/jpeg, para que el .deb
// sólo dependa de libc/libstdc++ y no haya que tocar el BSP.

#define LV_COLOR_DEPTH                     32
#define LV_COLOR_16_SWAP                   0

#define LV_USE_STDLIB_MALLOC               LV_STDLIB_BUILTIN
#define LV_MEM_SIZE                        (4U * 1024U * 1024U)
#define LV_USE_FLOAT                       1

#define LV_USE_OS                          LV_OS_PTHREAD
#define LV_DRAW_THREAD_STACK_SIZE          (64U * 1024U)

#define LV_USE_OBSERVER                    1

#define LV_USE_LOG                         1
#define LV_LOG_LEVEL                       LV_LOG_LEVEL_ERROR
#define LV_LOG_PRINTF                      1

#define LV_USE_ASSERT_NULL                 1
#define LV_USE_ASSERT_MALLOC               1

// Pantalla: framebuffer (no DRM, no OpenGL)
#define APP_USE_DRM                        0
#define LV_USE_LINUX_DRM                   0
#define LV_USE_LINUX_FBDEV                 1
#define LV_LINUX_FBDEV_RENDER_MODE         LV_DISPLAY_RENDER_MODE_PARTIAL
#define LV_LINUX_FBDEV_BUFFER_COUNT        0
#define LV_LINUX_FBDEV_BUFFER_SIZE         60
#define LV_LINUX_FBDEV_MMAP                1
#define LV_DEF_REFR_PERIOD                 16
#define LV_USE_DRAW_OPENGLES               0

// Entrada: teclado por evdev (el nuestro propio lo lee directamente)
#define LV_USE_EVDEV                       0

#define LV_USE_FS_STDIO                    1
#define LV_FS_STDIO_LETTER                 'A'
#define LV_FS_DEFAULT_DRIVER_LETTER        'A'

// Fuentes: sólo las integradas de LVGL (sin freetype)
#define LV_USE_FREETYPE                    0
#define LV_USE_TINY_TTF                    0
#define LV_FONT_MONTSERRAT_12              1
#define LV_FONT_MONTSERRAT_14              1
#define LV_FONT_MONTSERRAT_16              1
#define LV_FONT_MONTSERRAT_18              1
#define LV_FONT_MONTSERRAT_20              1
#define LV_FONT_DEFAULT                    &lv_font_montserrat_14
#define LV_USE_FONT_PLACEHOLDER            1

// Sin decodificadores de imagen externos
#define LV_USE_LIBPNG                      0
#define LV_USE_LODEPNG                     0
#define LV_USE_BMP                         0
#define LV_USE_LIBJPEG_TURBO               0
#define LV_USE_TJPGD                       0
#define LV_USE_SNAPSHOT                    0

#define LV_USE_THEME_DEFAULT               1
#define LV_THEME_DEFAULT_DARK              1

#define LV_USE_FLEX                        1
#define LV_USE_GRID                        1

#define LV_USE_LABEL                       1
#define LV_USE_BUTTON                      1
#define LV_USE_LIST                        1
#define LV_USE_TEXTAREA                    1

#define LV_USE_PERF_MONITOR                0
#define LV_USE_SYSMON                      0

#define LV_DRAW_BUF_ALIGN                  4
#define LV_DRAW_BUF_STRIDE_ALIGN           4
