/*
 * Copyright (C) 2014-2015 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of the Jolla Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gofono_modem.h"
#include "gofono_connmgr.h"
#include "gofono_simmgr.h"
#include "gofono_netreg.h"
#include "gofono_names.h"

#include <glib-unix.h>
#include <gutil_log.h>

#define RET_OK          (0)
#define RET_NOTFOUND    (1)
#define RET_NOTPRESENT  (2)
#define RET_ERR         (3)

typedef struct app {
    const char* path;
    GMainLoop* loop;
    OfonoObject* object;
    int ret;
} App;

static
void
object_dump_property_value(
    OfonoObject* object,
    const char* key,
    GVariant* value)
{
    gchar* text = g_variant_print(value, FALSE);
    printf("%s: %s\n", key, text);
    g_free(text);
}

static
void
object_dump_property(
    OfonoObject* object,
    const char* key)
{
    object_dump_property_value(object, key,
        ofono_object_get_property(object, key, NULL));
}

static
void
object_dump_properties(
    OfonoObject* object)
{
    guint i;
    GPtrArray* keys = ofono_object_get_property_keys(object);
    for (i=0; i<keys->len; i++) {
        object_dump_property(object, keys->pdata[i]);
    }
    g_ptr_array_unref(keys);
}

static
void
object_property_changed_handler(
    OfonoObject* object,
    const char* key,
    GVariant* value,
    void* arg)
{
    object_dump_property_value(object, key, value);
}

static
void
object_valid_changed(
    OfonoObject* object)
{
    printf("%s %s[%s]\n", object->valid ? "+++" : "---",
        object->intf, object->path);
    if (object->valid) object_dump_properties(object);
}

static
void
object_valid_changed_handler(
    OfonoObject* object,
    void* arg)
{
    object_valid_changed(object);
}

static
gboolean
app_signal(
    gpointer arg)
{
    GMainLoop* loop = arg;
    GINFO("Caught signal, shutting down...");
    g_idle_add((GSourceFunc)g_main_loop_quit, loop);
    return FALSE;
}

static
int
app_run(
    App* app)
{
    guint sigterm_id, sigint_id;
    gulong valid_id = ofono_object_add_valid_changed_handler(app->object,
        object_valid_changed_handler, app /* unused */);
    gulong property_id = ofono_object_add_property_changed_handler(app->object,
        object_property_changed_handler, NULL, app /* unused */);
    if (app->object->valid) object_valid_changed(app->object);

    app->loop = g_main_loop_new(NULL, FALSE);
    sigterm_id = g_unix_signal_add(SIGTERM, app_signal, app->loop);
    sigint_id = g_unix_signal_add(SIGINT, app_signal, app->loop);

    g_main_loop_run(app->loop);
    g_source_remove(sigterm_id);
    g_source_remove(sigint_id);

    ofono_object_remove_handler(app->object, valid_id);
    ofono_object_remove_handler(app->object, property_id);
    ofono_object_unref(app->object);
    g_main_loop_unref(app->loop);
    return app->ret;
}

static
OfonoObject*
object_create(
    const char* intf,
    const char* path,
    gboolean modem_intf)
{
    if (!strcmp(intf, OFONO_CONNMGR_INTERFACE_NAME)) {
        return &ofono_connmgr_new(path)->intf.object;
    } else if (!strcmp(intf, OFONO_SIMMGR_INTERFACE_NAME)) {
        return &ofono_simmgr_new(path)->intf.object;
    } else if (!strcmp(intf, OFONO_NETREG_INTERFACE_NAME)) {
        return &ofono_netreg_new(path)->intf.object;
    } else if (!strcmp(intf, OFONO_MODEM_INTERFACE_NAME)) {
        return &ofono_modem_new(path)->object;
    } else if (modem_intf) {
        return &ofono_modem_interface_new(intf, path)->object;
    } else {
        return ofono_object_new(intf, path);
    }
}

static
gboolean
app_init(
    App* app,
    int argc,
    char* argv[])
{
    gboolean ok = FALSE;
    gboolean verbose = FALSE;
    gboolean modem_intf = FALSE;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "modem-interface", 'm', 0, G_OPTION_ARG_NONE, &modem_intf,
          "Assume modem interface", NULL },
        { NULL }
    };
    GError* error = NULL;
    GOptionContext* options = g_option_context_new("INTERFACE PATH");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc == 3) {
            const char* name = argv[1];
            if (verbose) gutil_log_default.level = GLOG_LEVEL_VERBOSE;
            app->object = object_create(name, argv[2], modem_intf);
            ok = TRUE;
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    return ok;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    App app;
    memset(&app, 0, sizeof(app));
    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDERR, "ofono-monitor");
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;
    if (app_init(&app, argc, argv)) {
        ret = app_run(&app);
    }
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
