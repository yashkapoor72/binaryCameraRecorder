/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cuda-gst.h"
#include "gstcudanvrtc.h"
#include "gstcudaloader.h"
#include <nvrtc.h>
#include <gmodule.h>

GST_DEBUG_CATEGORY_STATIC (gst_cuda_nvrtc_debug);
#define GST_CAT_DEFAULT gst_cuda_nvrtc_debug

#ifndef G_OS_WIN32
#define NVRTC_LIBNAME "libnvrtc.so"
#else
#define NVRTC_LIBNAME "nvrtc64_%d%d_0.dll"
#endif

#define LOAD_SYMBOL(name,func) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), fname, g_module_error()); \
    goto error; \
  } \
} G_STMT_END;

/* *INDENT-OFF* */
typedef struct _GstCudaNvrtcVTable
{
  gboolean loaded;

  nvrtcResult (*NvrtcCompileProgram) (nvrtcProgram prog, int numOptions,
      const char **options);
  nvrtcResult (*NvrtcCreateProgram) (nvrtcProgram * prog, const char *src,
      const char *name, int numHeaders, const char **headers,
      const char **includeNames);
  nvrtcResult (*NvrtcDestroyProgram) (nvrtcProgram * prog);
  nvrtcResult (*NvrtcGetPTX) (nvrtcProgram prog, char *ptx);
  nvrtcResult (*NvrtcGetPTXSize) (nvrtcProgram prog, size_t * ptxSizeRet);
  nvrtcResult (*NvrtcGetProgramLog) (nvrtcProgram prog, char *log);
  nvrtcResult (*NvrtcGetProgramLogSize) (nvrtcProgram prog,
      size_t * logSizeRet);
} GstCudaNvrtcVTable;
/* *INDENT-ON* */

static GstCudaNvrtcVTable gst_cuda_nvrtc_vtable = { 0, };

static gboolean
gst_cuda_nvrtc_load_library_once (void)
{
  GModule *module = NULL;
  gchar *filename = NULL;
  const gchar *filename_env;
  const gchar *fname;
  gint cuda_version;
  GstCudaNvrtcVTable *vtable;

  CuDriverGetVersion (&cuda_version);

  fname = filename_env = g_getenv ("GST_CUDA_NVRTC_LIBNAME");
  if (filename_env)
    module = g_module_open (filename_env, G_MODULE_BIND_LAZY);

  if (!module) {
#ifndef G_OS_WIN32
    filename = g_strdup (NVRTC_LIBNAME);
    fname = filename;
    module = g_module_open (filename, G_MODULE_BIND_LAZY);
#else
    /* XXX: On Windows, minor version of nvrtc library might not be exactly
     * same as CUDA library */
    {
      gint cuda_major_version = cuda_version / 1000;
      gint cuda_minor_version = (cuda_version % 1000) / 10;
      gint minor_version;

      for (minor_version = cuda_minor_version; minor_version >= 0;
          minor_version--) {
        g_free (filename);
        filename = g_strdup_printf (NVRTC_LIBNAME, cuda_major_version,
            minor_version);
        fname = filename;

        module = g_module_open (filename, G_MODULE_BIND_LAZY);
        if (module) {
          GST_INFO ("%s is available", filename);
          break;
        }

        GST_DEBUG ("Couldn't open library %s", filename);
      }
    }
#endif
  }

  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    g_free (filename);
    return FALSE;
  }

  vtable = &gst_cuda_nvrtc_vtable;

  LOAD_SYMBOL (nvrtcCompileProgram, NvrtcCompileProgram);
  LOAD_SYMBOL (nvrtcCreateProgram, NvrtcCreateProgram);
  LOAD_SYMBOL (nvrtcDestroyProgram, NvrtcDestroyProgram);
  LOAD_SYMBOL (nvrtcGetPTX, NvrtcGetPTX);
  LOAD_SYMBOL (nvrtcGetPTXSize, NvrtcGetPTXSize);
  LOAD_SYMBOL (nvrtcGetProgramLog, NvrtcGetProgramLog);
  LOAD_SYMBOL (nvrtcGetProgramLogSize, NvrtcGetProgramLogSize);

  vtable->loaded = TRUE;
  g_free (filename);

  return TRUE;

error:
  g_module_close (module);
  g_free (filename);

  return FALSE;
}

/**
 * gst_cuda_nvrtc_load_library:
 *
 * Loads the nvrtc library.
 *
 * Returns: %TRUE if the library could be loaded, %FALSE otherwise
 *
 * Since: 1.22
 */
gboolean
gst_cuda_nvrtc_load_library (void)
{
  static gsize init_once = 0;

  if (g_once_init_enter (&init_once)) {
    GST_DEBUG_CATEGORY_INIT (gst_cuda_nvrtc_debug, "cudanvrtc", 0,
        "CUDA runtime compiler");
    if (gst_cuda_load_library ())
      gst_cuda_nvrtc_load_library_once ();
    g_once_init_leave (&init_once, 1);
  }

  return gst_cuda_nvrtc_vtable.loaded;
}

/* *INDENT-OFF* */
static nvrtcResult
NvrtcCompileProgram (nvrtcProgram prog, int numOptions, const char **options)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcCompileProgram != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcCompileProgram (prog, numOptions, options);
}

static nvrtcResult
NvrtcCreateProgram (nvrtcProgram * prog, const char *src, const char *name,
    int numHeaders, const char **headers, const char **includeNames)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcCreateProgram != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcCreateProgram (prog, src, name, numHeaders,
      headers, includeNames);
}

static nvrtcResult
NvrtcDestroyProgram (nvrtcProgram * prog)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcDestroyProgram != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcDestroyProgram (prog);
}

static nvrtcResult
NvrtcGetPTX (nvrtcProgram prog, char *ptx)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcGetPTX != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcGetPTX (prog, ptx);
}

static nvrtcResult
NvrtcGetPTXSize (nvrtcProgram prog, size_t *ptxSizeRet)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcGetPTXSize != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcGetPTXSize (prog, ptxSizeRet);
}

static nvrtcResult
NvrtcGetProgramLog (nvrtcProgram prog, char *log)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcGetProgramLog != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcGetProgramLog (prog, log);
}

static nvrtcResult
NvrtcGetProgramLogSize (nvrtcProgram prog, size_t *logSizeRet)
{
  g_assert (gst_cuda_nvrtc_vtable.NvrtcGetProgramLogSize != NULL);

  return gst_cuda_nvrtc_vtable.NvrtcGetProgramLogSize (prog, logSizeRet);
}
/* *INDENT-ON* */

/**
 * gst_cuda_nvrtc_compile:
 * @source: Source code to compile
 *
 * Since: 1.22
 */
gchar *
gst_cuda_nvrtc_compile (const gchar * source)
{
  nvrtcProgram prog;
  nvrtcResult ret;
  CUresult curet;
  const gchar *opts[] = { "--gpu-architecture=compute_30" };
  gsize ptx_size;
  gchar *ptx = NULL;
  int driverVersion;

  g_return_val_if_fail (source != NULL, NULL);

  if (!gst_cuda_nvrtc_load_library ()) {
    return NULL;
  }

  GST_TRACE ("CUDA kernel source \n%s", source);

  curet = CuDriverGetVersion (&driverVersion);
  if (curet != CUDA_SUCCESS) {
    GST_ERROR ("Failed to query CUDA Driver version, ret %d", curet);
    return NULL;
  }

  GST_DEBUG ("CUDA Driver Version %d.%d", driverVersion / 1000,
      (driverVersion % 1000) / 10);

  ret = NvrtcCreateProgram (&prog, source, NULL, 0, NULL, NULL);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("couldn't create nvrtc program, ret %d", ret);
    return NULL;
  }

  /* Starting from CUDA 11, the lowest supported architecture is 5.2 */
  if (driverVersion >= 11000)
    opts[0] = "--gpu-architecture=compute_52";

  ret = NvrtcCompileProgram (prog, 1, opts);
  if (ret != NVRTC_SUCCESS) {
    gsize log_size;

    GST_ERROR ("couldn't compile nvrtc program, ret %d", ret);
    if (NvrtcGetProgramLogSize (prog, &log_size) == NVRTC_SUCCESS &&
        log_size > 0) {
      gchar *compile_log = g_alloca (log_size);
      if (NvrtcGetProgramLog (prog, compile_log) == NVRTC_SUCCESS) {
        GST_ERROR ("nvrtc compile log %s", compile_log);
      }
    }

    goto error;
  }

  ret = NvrtcGetPTXSize (prog, &ptx_size);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("unknown ptx size, ret %d", ret);

    goto error;
  }

  ptx = g_malloc0 (ptx_size);
  ret = NvrtcGetPTX (prog, ptx);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("couldn't get ptx, ret %d", ret);
    g_free (ptx);

    goto error;
  }

  NvrtcDestroyProgram (&prog);

  GST_TRACE ("compiled CUDA PTX %s\n", ptx);

  return ptx;

error:
  NvrtcDestroyProgram (&prog);

  return NULL;
}
