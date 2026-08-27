/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 *
 * Debug features of OpenGL.
 */

#include "BLI_compiler_attrs.h"
#include "BLI_string.h"
#include "BLI_system.h"
#include "BLI_utildefines.h"

#include "BKE_global.hh"

#include "GPU_debug.hh"
#include "GPU_platform.hh"

#include "mtl_context.hh"
#include "mtl_debug.hh"

#include "CLG_log.h"

#include <utility>

/* ISS-017/018: NSWorkspace (AppKit) is needed to auto-open the captured .gputrace in Xcode. */
#import <AppKit/AppKit.h>

namespace blender::gpu::debug {

CLG_LogRef LOG = {"gpu.debug.metal"};

void mtl_debug_init()
{
  CLOG_ENSURE(&LOG);
}

}  // namespace blender::gpu::debug

namespace blender::gpu {

/* -------------------------------------------------------------------- */
/** \name Debug Groups
 *
 * Useful for debugging through XCode GPU Debugger. This ensures all the API calls grouped into
 * "passes".
 * \{ */

void MTLContext::debug_group_begin(const char *name, int index)
{
  if (G.debug & G_DEBUG_GPU) {
    this->main_command_buffer.push_debug_group(name, index);
  }
}

void MTLContext::debug_group_end()
{
  if (G.debug & G_DEBUG_GPU) {
    this->main_command_buffer.pop_debug_group();
  }
}

/* ISS-017/018 Xcode auto-open: path of the .gputrace written by the current capture (if any),
 * so debug_capture_end() can open it in Xcode after stopCapture finalises the file. nil when the
 * capture goes straight to Xcode (developer-tools destination, i.e. launched from Xcode). */
static NSString *g_goo_capture_trace_path = nil;

bool MTLContext::debug_capture_begin(const char * /*title*/)
{
  MTLCaptureManager *capture_manager = [MTLCaptureManager sharedCaptureManager];
  if (!capture_manager) {
    /* Early exit if frame capture is disabled. */
    return false;
  }
  MTLCaptureDescriptor *capture_descriptor = [[MTLCaptureDescriptor alloc] init];
  capture_descriptor.captureObject = this->device;

  /* "F12 だけで Xcode が開く" path (ISS-017/018 investigation). When NOT launched from Xcode the
   * developer-tools destination is unavailable, so a normal terminal launch would silently capture
   * nothing. If GOO_GPU_CAPTURE is set, instead write the trace to a unique .gputrace file and let
   * debug_capture_end() hand it to the system (Xcode opens .gputrace). This makes the workflow:
   *   METAL_CAPTURE_ENABLED=1 GOO_GPU_CAPTURE=1 Blender --debug-gpu <blend>
   * then every F12 auto-writes a trace and pops it open in Xcode — no Xcode-launched scheme needed.
   * When developer-tools IS available (launched from Xcode) we keep the default destination so the
   * capture opens live in Xcode as before. */
  g_goo_capture_trace_path = nil;
  const bool want_autoopen = (getenv("GOO_GPU_CAPTURE") != nullptr);
  const bool has_devtools = [capture_manager
      supportsDestination:MTLCaptureDestinationDeveloperTools];
  const bool has_tracefile = [capture_manager
      supportsDestination:MTLCaptureDestinationGPUTraceDocument];
  if (want_autoopen && !has_devtools && has_tracefile) {
    NSString *path = [NSString stringWithFormat:@"%@/goo_capture_%d_%.0f.gputrace",
                                                NSTemporaryDirectory(),
                                                (int)getpid(),
                                                [[NSDate date] timeIntervalSince1970]];
    /* startCapture fails if the URL already exists; the pid+time stamp keeps it unique. */
    capture_descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
    capture_descriptor.outputURL = [NSURL fileURLWithPath:path];
    g_goo_capture_trace_path = [path retain];
  }

  NSError *error;
  if (![capture_manager startCaptureWithDescriptor:capture_descriptor error:&error]) {
    NSLog(@"Failed to start Metal frame capture, error %@", error);
    [g_goo_capture_trace_path release];
    g_goo_capture_trace_path = nil;
    return false;
  }
  return true;
}

void MTLContext::debug_capture_end()
{
  MTLCaptureManager *capture_manager = [MTLCaptureManager sharedCaptureManager];
  if (!capture_manager) {
    /* Early exit if frame capture is disabled. */
    return;
  }
  [capture_manager stopCapture];

  /* If we captured to a .gputrace file (terminal launch), open it now — macOS routes .gputrace to
   * Xcode, so the Metal debugger pops up automatically right after F12. */
  if (g_goo_capture_trace_path != nil) {
    NSURL *url = [NSURL fileURLWithPath:g_goo_capture_trace_path];
    NSLog(@"[GOOENG] Metal capture written: %@ — opening in Xcode", g_goo_capture_trace_path);
    [[NSWorkspace sharedWorkspace] openURL:url];
    [g_goo_capture_trace_path release];
    g_goo_capture_trace_path = nil;
  }
}

void *MTLContext::debug_capture_scope_create(const char *name)
{
  /* Create a capture scope visible to xCode Metal Frame capture utility. */
  MTLCaptureManager *capture_manager = [MTLCaptureManager sharedCaptureManager];
  if (!capture_manager) {
    /* Early exit if frame capture is disabled. */
    return nullptr;
  }
  id<MTLCaptureScope> capture_scope = [capture_manager newCaptureScopeWithDevice:this->device];
  capture_scope.label = [NSString stringWithUTF8String:name];
  [capture_scope retain];

  return reinterpret_cast<void *>(capture_scope);
}

bool MTLContext::debug_capture_scope_begin(void *scope)
{
  /* Declare opening boundary of scope.
   * When scope is selected for capture, GPU commands between begin/end scope will be captured. */
  [(id<MTLCaptureScope>)scope beginScope];

  MTLCaptureManager *capture_manager = [MTLCaptureManager sharedCaptureManager];
  return [capture_manager isCapturing];
}

void MTLContext::debug_capture_scope_end(void *scope)
{
  [(id<MTLCaptureScope>)scope endScope];
}

/** \} */

}  // namespace blender::gpu
