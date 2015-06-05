/* GStreamer
 *
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

//in sync with definition in audiofxbasefirfilter.c implementation
#define FFT_THRESHOLD 32

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

static gboolean have_eos = FALSE;

static gboolean
on_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_EOS:
      have_eos = TRUE;
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static gint num_filter_elements = 6;

static void
on_rate_changed_multi_kernel (GstElement * element, gint rate,
    gpointer user_data)
{
  GValueArray *two_channel_kernel;
  GValueArray *va;
  GValue v = { 0, };
  GValue v_arr = { G_TYPE_VALUE_ARRAY };
  const guint num_channels = 2;
  guint i, j;

  fail_unless (rate > 0);
  g_value_init (&v, G_TYPE_DOUBLE);

  two_channel_kernel = g_value_array_new (num_channels);
  for (i = 0; i < num_channels; i++) {
    va = g_value_array_new (num_filter_elements);
    for (j = 0; j < (num_filter_elements - 1); j++) {
      g_value_set_double (&v, 0.0);
      g_value_array_append (va, &v);
      g_value_reset (&v);
    }

    g_value_set_double (&v, 1.0);
    g_value_array_append (va, &v);
    g_value_reset (&v);

    g_value_take_boxed (&v_arr, va);
    g_value_array_append (two_channel_kernel, &v_arr);
  }
  g_object_set (G_OBJECT (element), "multi-channel-kernel", two_channel_kernel,
      NULL);
  g_value_array_free (two_channel_kernel);
}

static void
on_rate_changed_kernel (GstElement * element, gint rate, gpointer user_data)
{
  GValueArray *va;
  GValue v = { 0, };
  gint i;

  fail_unless (rate > 0);

  va = g_value_array_new (num_filter_elements);

  g_value_init (&v, G_TYPE_DOUBLE);
  for (i = 0; i < (num_filter_elements - 1); i++) {
    g_value_set_double (&v, 0.0);
    g_value_array_append (va, &v);
    g_value_reset (&v);
  }
  g_value_set_double (&v, 1.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);

  g_object_set (G_OBJECT (element), "kernel", va, NULL);

  g_value_array_free (va);
}

static gboolean have_data = FALSE;

static void
on_handoff (GstElement * object, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  if (!have_data) {
    GstMapInfo map;
    gdouble *data;
    gdouble checkValue;
    gint num_double_elems;
    gint i, first_non_null_index;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = (gdouble *) map.data;

    num_double_elems = num_filter_elements * sizeof (gdouble);

    fail_unless (map.size > num_double_elems);
    first_non_null_index = num_filter_elements - 1;

    for (i = 0; i < first_non_null_index; i++) {
      checkValue = fabs (data[i]);
      fail_unless (checkValue < (4 * DBL_EPSILON));
    }
    checkValue = fabs (data[i]);
    fail_unless (checkValue > (4.0f * DBL_EPSILON));

    gst_buffer_unmap (buffer, &map);
    have_data = TRUE;
  }
}

static GstCaps *
get_caps (void)
{
  GstCaps *caps;
#if G_BYTE_ORDER == G_BIG_ENDIAN
  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "F64BE", NULL);
#else
  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "F64LE", NULL);
#endif
  return caps;
}

#define PREPARE_TEST_ENV(test_func, num_kernel_elem) \
GstElement *pipeline, *src, *cfilter, *filter, *sink;\
  GstCaps *caps;\
  GstBus *bus;\
  GMainLoop *loop;\
\
  have_data = FALSE;\
  have_eos = FALSE;\
  num_filter_elements = num_kernel_elem;\
\
  caps=get_caps();\
\
  pipeline = gst_element_factory_make ("pipeline", NULL);\
  fail_unless (pipeline != NULL);\
\
  src = gst_element_factory_make ("audiotestsrc", NULL);\
  fail_unless (src != NULL);\
  g_object_set (G_OBJECT (src), "num-buffers", num_kernel_elem*200, NULL);\
\
  cfilter = gst_element_factory_make ("capsfilter", NULL);\
  fail_unless (cfilter != NULL);\
\
  g_object_set (G_OBJECT (cfilter), "caps", caps, NULL);\
  gst_caps_unref (caps);\
\
  filter = gst_element_factory_make ("audiofirfilter", NULL);\
  fail_unless (filter != NULL);\
  \
  sink = gst_element_factory_make ("fakesink", NULL);\
    fail_unless (sink != NULL);\
    g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);\
\
  /* register property related callbacks : could be pulled out to start test??*/ \
  if( strstr (G_STRINGIFY (test_func), "test_pipeline_kernel" ) ){\
    g_signal_connect (G_OBJECT (filter), "rate-changed",\
        G_CALLBACK (on_rate_changed_kernel), NULL);\
  }\
  else if( strstr (G_STRINGIFY (test_func), "test_pipeline_multi_kernel" )) {\
	g_signal_connect (G_OBJECT (filter), "rate-changed",\
        G_CALLBACK (on_rate_changed_multi_kernel), NULL);\
	}\
	else\
	{\
		fail_unless(0);\
	}\
	g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (on_handoff), NULL);\
\
  gst_bin_add_many (GST_BIN (pipeline), src, cfilter, filter, sink, NULL);\
  fail_unless (gst_element_link_many (src, cfilter, filter, sink, NULL));\
\
  loop = g_main_loop_new (NULL, FALSE);\
\
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));\
  gst_bus_add_signal_watch (bus);\
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (on_message), loop);\
  gst_object_unref (GST_OBJECT (bus));\
\
  fail_if (gst_element_set_state (pipeline,\
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);\
\
  g_main_loop_run (loop);\
\
  fail_unless (have_data);\
  fail_unless (have_eos);\
\
  fail_unless (gst_element_set_state (pipeline,\
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);\
\
  g_main_loop_unref (loop);\
  gst_object_unref (pipeline);\


GST_START_TEST (test_pipeline_kernel_time)
{
  PREPARE_TEST_ENV (test_pipeline_kernel_time, 6)
}

GST_END_TEST;


GST_START_TEST (test_pipeline_multi_kernel_time)
{
  PREPARE_TEST_ENV (test_pipeline_multi_kernel_time, 6)
}

GST_END_TEST;

GST_START_TEST (test_pipeline_kernel_freq)
{
  PREPARE_TEST_ENV (test_pipeline_kernel_freq, FFT_THRESHOLD * 2)
}

GST_END_TEST;


GST_START_TEST (test_pipeline_multi_kernel_freq)
{
  PREPARE_TEST_ENV (test_pipeline_multi_kernel_freq, FFT_THRESHOLD * 2)
}

GST_END_TEST;

static Suite *
audiofirfilter_suite (void)
{
  Suite *s = suite_create ("audiofirfilter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pipeline_kernel_time);
  tcase_add_test (tc_chain, test_pipeline_multi_kernel_time);
  tcase_add_test (tc_chain, test_pipeline_kernel_freq);
  tcase_add_test (tc_chain, test_pipeline_multi_kernel_freq);
  return s;
}

GST_CHECK_MAIN (audiofirfilter);
