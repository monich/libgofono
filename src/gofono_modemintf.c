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

#include "gofono_modemintf_p.h"
#include "gofono_modem_p.h"
#include "gofono_names.h"
#include "gofono_log.h"

/* Object definition */
enum modem_handler_id {
    MODEM_HANDLER_INTERFACES_CHANGED,
    MODEM_HANDLER_VALID_CHANGED,
    MODEM_HANDLER_COUNT
};

struct ofono_modem_interface_priv {
    gboolean present;
    gulong modem_handler_id[MODEM_HANDLER_COUNT];
};

G_DEFINE_TYPE(OfonoModemInterface, ofono_modem_interface, OFONO_TYPE_OBJECT)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
ofono_modem_interface_check(
    OfonoModemInterface* self)
{
    OfonoObject* object = &self->object;
    GPtrArray* ifs = self->modem->interfaces;
    OfonoModemInterfacePriv* priv = self->priv;
    gboolean present = FALSE;
    if (self->modem->object.valid && ifs) {
        guint i;
        for (i=0; i<ifs->len; i++) {
            const char* ifname = ifs->pdata[i];
            if (ifname && !strcmp(ifname, object->intf)) {
                present = TRUE;
                break;
            }
        }
    }
    if (priv->present != present) {
        priv->present = present;
        ofono_object_set_invalid(object, !present);
    }
}

static
void
ofono_modem_interface_modem_changed(
    OfonoModem* modem,
    void* arg)
{
    ofono_modem_interface_check(OFONO_MODEM_INTERFACE(arg));
}

/*==========================================================================*
 * API
 *==========================================================================*/

OfonoModemInterface*
ofono_modem_interface_new(
    const char* ifname,
    const char* path)
{
    OfonoModem* modem = ofono_modem_new(path);
    OfonoModemInterface* intf = ofono_modem_get_interface(modem, ifname);
    if (intf) {
        g_object_ref(intf);
    } else {
        intf = g_object_new(OFONO_TYPE_MODEM_INTERFACE, NULL);
        ofono_modem_interface_initialize(intf, ifname, path);
    }
    ofono_modem_unref(modem);
    return intf;
}

void
ofono_modem_interface_initialize(
    OfonoModemInterface* self,
    const char* intf,
    const char* path)
{
    OfonoModemInterfacePriv* priv = self->priv;
    /* Instantiate the modem first to make sure that modem gets initialized
     * before GetProperties query completes for the modem interface object. */
    GASSERT(!self->modem);
    self->modem = ofono_modem_new(path);
    ofono_object_initialize(&self->object, intf, path);
    priv->modem_handler_id[MODEM_HANDLER_INTERFACES_CHANGED] =
        ofono_modem_add_interfaces_changed_handler(self->modem,
            ofono_modem_interface_modem_changed, self);
    priv->modem_handler_id[MODEM_HANDLER_VALID_CHANGED] =
        ofono_modem_add_valid_changed_handler(self->modem,
            ofono_modem_interface_modem_changed, self);
    ofono_modem_interface_check(self);
}

OfonoModemInterface*
ofono_modem_interface_ref(
    OfonoModemInterface* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(OFONO_MODEM_INTERFACE(self));
        return self;
    }
    return NULL;
}

void
ofono_modem_interface_unref(
    OfonoModemInterface* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(OFONO_MODEM_INTERFACE(self));
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Reset the object after it has become invalid.
 */
static
void
ofono_modem_interface_invalidate(
    OfonoObject* obj)
{
    OfonoModemInterface* self = OFONO_MODEM_INTERFACE(obj);
    self->priv->present = FALSE;
    OFONO_OBJECT_CLASS(ofono_modem_interface_parent_class)->fn_invalidate(obj);
}

/**
 * Per instance initializer
 */
static
void
ofono_modem_interface_init(
    OfonoModemInterface* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        OFONO_TYPE_MODEM_INTERFACE, OfonoModemInterfacePriv);
}

/**
 * Final stage of deinitialization
 */
static
void
ofono_modem_interface_finalize(
    GObject* object)
{
    OfonoModemInterface* self = OFONO_MODEM_INTERFACE(object);
    OfonoModemInterfacePriv* priv = self->priv;
    unsigned int i;
    for (i=0; i<G_N_ELEMENTS(priv->modem_handler_id); i++) {
        ofono_modem_remove_handler(self->modem, priv->modem_handler_id[i]);
    }
    ofono_modem_unref(self->modem);
    G_OBJECT_CLASS(ofono_modem_interface_parent_class)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
ofono_modem_interface_class_init(
    OfonoModemInterfaceClass* klass)
{
    g_type_class_add_private(klass, sizeof(OfonoModemInterfacePriv));
    klass->object.fn_invalidate = ofono_modem_interface_invalidate;
    G_OBJECT_CLASS(klass)->finalize = ofono_modem_interface_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
