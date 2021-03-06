#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glsym/glsym.h>
#include <libretro.h>

#include "../../game.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
struct retro_hw_render_callback hw_render;

#if defined(HAVE_PSGL)
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER_OES
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_OES
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#elif defined(OSX_PPC)
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER_EXT
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE_EXT
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0_EXT
#else
#define RARCH_GL_FRAMEBUFFER GL_FRAMEBUFFER
#define RARCH_GL_FRAMEBUFFER_COMPLETE GL_FRAMEBUFFER_COMPLETE
#define RARCH_GL_COLOR_ATTACHMENT0 GL_COLOR_ATTACHMENT0
#endif

#define BASE_WIDTH 320
#define BASE_HEIGHT 240
#ifdef HAVE_OPENGLES
#define MAX_WIDTH 1024
#define MAX_HEIGHT 1024
#else
#define MAX_WIDTH 2560
#define MAX_HEIGHT 1440
#endif

#define SND_FRAME_SIZE  4

static unsigned FRAMERATE     = 60;
static unsigned SND_RATE      = 44100;

static unsigned width         = BASE_WIDTH;
static unsigned height        = BASE_HEIGHT;

static bool force_linear      = true;
static bool disable_water     = false;

Sound::Frame *sndData;

char levelpath[255];
char basedir[1024];
char Stream::cacheDir[255];
char Stream::contentDir[255];

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;


void retro_init(void)
{
   const char *sysdir = NULL;
   Stream::contentDir[0] = Stream::cacheDir[0] = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir))
   {
#ifdef _WIN32
      char slash = '\\';
#else
      char slash = '/';
#endif
      sprintf(Stream::cacheDir, "%s%copenlara-", sysdir, slash);
   }
}

void retro_deinit(void)
{
   Stream::contentDir[0] = Stream::cacheDir[0] = 0;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "OpenLara";
   info->library_version  = "v1";
   info->need_fullpath    = false;
   info->valid_extensions = "phd|psx"; // Anything is fine, we don't care.
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->timing = (struct retro_system_timing) {
      .fps = (float)FRAMERATE,
      .sample_rate = (float)SND_RATE,
   };

   info->geometry = (struct retro_game_geometry) {
      .base_width   = BASE_WIDTH,
      .base_height  = BASE_HEIGHT,
      .max_width    = MAX_WIDTH,
      .max_height   = MAX_HEIGHT,
      .aspect_ratio = 4.0 / 3.0,
   };
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "openlara_framerate",
         "Framerate (restart); 60fps|90fps|120fps|144fps|30fps",
      },
      {
         "openlara_resolution",
         "Internal resolution (restart); 320x240|360x480|480x272|512x384|512x512|640x240|640x448|640x480|720x576|800x600|960x720|1024x768|1024x1024|1280x720|1280x960|1600x1200|1920x1080|1920x1440|1920x1600|2048x2048|2560x1440",
      },
      {
         "openlara_texture_filtering",
         "Texture filtering (restart); Bilinear filtering|Nearest",
      },
      {
         "openlara_water_effects",
         "Water effects (restart); enabled|disabled",
      },
      { NULL, NULL },
   };

   bool no_rom = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_rom);
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static void update_variables(bool first_startup)
{
   if (first_startup)
   {
      struct retro_variable var;

      var.key = "openlara_resolution";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         char *pch;
         char str[100];
         snprintf(str, sizeof(str), "%s", var.value);

         pch = strtok(str, "x");
         if (pch)
            width = strtoul(pch, NULL, 0);
         pch = strtok(NULL, "x");
         if (pch)
            height = strtoul(pch, NULL, 0);

         fprintf(stderr, "[openlara]: Got size: %u x %u.\n", width, height);
      }

      var.key = "openlara_framerate";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "30fps"))
            FRAMERATE     = 30;
         else if (!strcmp(var.value, "60fps"))
            FRAMERATE     = 60;
         else if (!strcmp(var.value, "90fps"))
            FRAMERATE     = 90;
         else if (!strcmp(var.value, "120fps"))
            FRAMERATE     = 120;
         else if (!strcmp(var.value, "144fps"))
            FRAMERATE     = 120;
      }
      else
         FRAMERATE     = 60;

      var.key = "openlara_water_effects";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "enabled"))
            disable_water     = false;
         else if (!strcmp(var.value, "disabled"))
            disable_water     = true;
      }
      else
         disable_water     = false;

      var.key = "openlara_texture_filtering";

      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "Bilinear filtering"))
            force_linear     = true;
         else if (!strcmp(var.value, "Nearest"))
            force_linear     = false;
      }
      else
         force_linear     = true;
   }
}

void retro_run(void)
{
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables(false);

   input_poll_cb();

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
      Input::setDown(InputKey::ikUp, true);
   else
      Input::setDown(InputKey::ikUp, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
      Input::setDown(InputKey::ikDown, true);
   else
      Input::setDown(InputKey::ikDown, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
      Input::setDown(InputKey::ikLeft, true);
   else
      Input::setDown(InputKey::ikLeft, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
      Input::setDown(InputKey::ikRight, true);
   else
      Input::setDown(InputKey::ikRight, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))
      Input::setDown(InputKey::ikSpace, true);
   else
      Input::setDown(InputKey::ikSpace, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))
      Input::setDown(InputKey::ikCtrl, true);
   else
      Input::setDown(InputKey::ikCtrl, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))
      Input::setDown(InputKey::ikS, true);
   else
      Input::setDown(InputKey::ikS, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
      Input::setDown(InputKey::ikAlt, true);
   else
      Input::setDown(InputKey::ikAlt, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))
      Input::setDown(InputKey::ikShift, true);
   else
      Input::setDown(InputKey::ikShift, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2))
      Input::setDown(InputKey::ikZ, true);
   else
      Input::setDown(InputKey::ikZ, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))
      Input::setDown(InputKey::ikX, true);
   else
      Input::setDown(InputKey::ikX, false);

   if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
      Input::setDown(InputKey::ikV, true);
   else
      Input::setDown(InputKey::ikV, false);

   int audio_frames = SND_RATE / FRAMERATE;
   int16_t *samples = (int16_t*)sndData;

   Sound::fill(sndData, audio_frames);

   while (audio_frames > 512)
   {
      audio_batch_cb(samples, 512);
      samples += 1024;
      audio_frames -= 512;
   }
   audio_batch_cb(samples, audio_frames);

   Game::update(1.0 / FRAMERATE);
   Core::defaultFBO = hw_render.get_current_framebuffer();
   Game::render();

   video_cb(RETRO_HW_FRAME_BUFFER_VALID, width, height, 0);
}

static void context_reset(void)
{
   char musicpath[255];
#ifdef _WIN32
   char slash = '\\';
#else
   char slash = '/';
#endif
   fprintf(stderr, "Context reset!\n");
   rglgen_resolve_symbols(hw_render.get_proc_address);

   sndData = new Sound::Frame[SND_RATE * 2 * sizeof(int16_t) / FRAMERATE];
   snprintf(musicpath, sizeof(musicpath), "%s%c05.ogg",
         basedir, slash);
   Game::init(levelpath, musicpath, disable_water, false);
}

static void context_destroy(void)
{
   fprintf(stderr, "Context destroy!\n");
   delete[] sndData;
   Game::free();
}

#ifdef HAVE_OPENGLES
static bool retro_init_hw_context(void)
{
#if defined(HAVE_OPENGLES_3_1)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES_VERSION;
   hw_render.version_major = 3;
   hw_render.version_minor = 1;
#elif defined(HAVE_OPENGLES3)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#endif
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = true;
   hw_render.stencil = true;
   hw_render.bottom_left_origin = true;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}
#else
static bool retro_init_hw_context(void)
{
#if defined(CORE)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
   hw_render.version_major = 3;
   hw_render.version_minor = 1;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = true;
   hw_render.stencil = true;
   hw_render.bottom_left_origin = true;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return false;

   return true;
}
#endif

static void extract_directory(char *buf, const char *path, size_t size)
{
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   char *base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
   {
      buf[0] = '.';
      buf[1] = '\0';
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   update_variables(true);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "XRGB8888 is not supported.\n");
      return false;
   }

   if (!retro_init_hw_context())
   {
      fprintf(stderr, "HW Context could not be initialized, exiting...\n");
      return false;
   }

   fprintf(stderr, "Loaded game!\n");
   (void)info;


   levelpath[0] = '\0';
   strcpy(levelpath, info->path);

   basedir[0] = '\0';
   extract_directory(basedir, info->path, sizeof(basedir));

   Core::width  = width;
   Core::height = height;
   Core::support.force_linear  = force_linear;

   return true;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_reset(void)
{}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

