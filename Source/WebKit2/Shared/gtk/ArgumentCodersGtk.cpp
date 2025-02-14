/*
 * Copyright (C) 2011 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ArgumentCodersGtk.h"

#include "DataReference.h"
#include "ShareableBitmap.h"
#include "WebCoreArgumentCoders.h"
#include <WebCore/DataObjectGtk.h>
#include <WebCore/DragData.h>
#include <WebCore/GraphicsContext.h>
#include <WebCore/GtkVersioning.h>
#include <WebCore/PlatformContextCairo.h>
#include <wtf/glib/GUniquePtr.h>

using namespace WebCore;
using namespace WebKit;

namespace IPC {

static void encodeImage(Encoder& encoder, const GdkPixbuf* pixbuf)
{
    IntSize imageSize(gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
    RefPtr<ShareableBitmap> bitmap = ShareableBitmap::createShareable(imageSize, ShareableBitmap::SupportsAlpha);
    auto graphicsContext = bitmap->createGraphicsContext();

    cairo_t* cr = graphicsContext->platformContext()->cr();
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
    cairo_paint(cr);

    ShareableBitmap::Handle handle;
    bitmap->createHandle(handle);

    encoder << handle;
}

static bool decodeImage(Decoder& decoder, GRefPtr<GdkPixbuf>& pixbuf)
{
    ShareableBitmap::Handle handle;
    if (!decoder.decode(handle))
        return false;

    RefPtr<ShareableBitmap> bitmap = ShareableBitmap::create(handle);
    if (!bitmap)
        return false;

    RefPtr<Image> image = bitmap->createImage();
    if (!image)
        return false;

    RefPtr<cairo_surface_t> surface = image->nativeImageForCurrentFrame();
    if (!surface)
        return false;

    pixbuf = adoptGRef(gdk_pixbuf_get_from_surface(surface.get(), 0, 0, cairo_image_surface_get_width(surface.get()), cairo_image_surface_get_height(surface.get())));
    if (!pixbuf)
        return false;

    return true;
}

void encode(Encoder& encoder, const DataObjectGtk* dataObject)
{
    bool hasText = dataObject->hasText();
    encoder << hasText;
    if (hasText)
        encoder << dataObject->text();

    bool hasMarkup = dataObject->hasMarkup();
    encoder << hasMarkup;
    if (hasMarkup)
        encoder << dataObject->markup();

    bool hasURL = dataObject->hasURL();
    encoder << hasURL;
    if (hasURL)
        encoder << dataObject->url().string();

    bool hasURIList = dataObject->hasURIList();
    encoder << hasURIList;
    if (hasURIList)
        encoder << dataObject->uriList();

    bool hasImage = dataObject->hasImage();
    encoder << hasImage;
    if (hasImage)
        encodeImage(encoder, dataObject->image());

    bool hasUnknownTypeData = dataObject->hasUnknownTypeData();
    encoder << hasUnknownTypeData;
    if (hasUnknownTypeData)
        encoder << dataObject->unknownTypes();

    bool canSmartReplace = dataObject->canSmartReplace();
    encoder << canSmartReplace;
}

bool decode(Decoder& decoder, RefPtr<DataObjectGtk>& dataObject)
{
    RefPtr<DataObjectGtk> data = DataObjectGtk::create();

    bool hasText;
    if (!decoder.decode(hasText))
        return false;
    if (hasText) {
        String text;
        if (!decoder.decode(text))
            return false;
        data->setText(text);
    }

    bool hasMarkup;
    if (!decoder.decode(hasMarkup))
        return false;
    if (hasMarkup) {
        String markup;
        if (!decoder.decode(markup))
            return false;
        data->setMarkup(markup);
    }

    bool hasURL;
    if (!decoder.decode(hasURL))
        return false;
    if (hasURL) {
        String url;
        if (!decoder.decode(url))
            return false;
        data->setURL(URL(URL(), url), String());
    }

    bool hasURIList;
    if (!decoder.decode(hasURIList))
        return false;
    if (hasURIList) {
        String uriList;
        if (!decoder.decode(uriList))
            return false;
        data->setURIList(uriList);
    }

    bool hasImage;
    if (!decoder.decode(hasImage))
        return false;
    if (hasImage) {
        GRefPtr<GdkPixbuf> image;
        if (!decodeImage(decoder, image))
            return false;
        data->setImage(image.get());
    }

    bool hasUnknownTypeData;
    if (!decoder.decode(hasUnknownTypeData))
        return false;
    if (hasUnknownTypeData) {
        HashMap<String, String> unknownTypes;
        if (!decoder.decode(unknownTypes))
            return false;

        auto end = unknownTypes.end();
        for (auto it = unknownTypes.begin(); it != end; ++it)
            data->setUnknownTypeData(it->key, it->value);
    }

    bool canSmartReplace;
    if (!decoder.decode(canSmartReplace))
        return false;
    data->setCanSmartReplace(canSmartReplace);

    dataObject = data;

    return true;
}

#if ENABLE(DRAG_SUPPORT)
void ArgumentCoder<DragData>::encode(Encoder& encoder, const DragData& dragData)
{
    encoder << dragData.clientPosition();
    encoder << dragData.globalPosition();
    encoder << static_cast<uint64_t>(dragData.draggingSourceOperationMask());
    encoder << static_cast<uint64_t>(dragData.flags());

    DataObjectGtk* platformData = dragData.platformData();
    encoder << static_cast<bool>(platformData);
    if (platformData)
        IPC::encode(encoder, platformData);
}

bool ArgumentCoder<DragData>::decode(Decoder& decoder, DragData& dragData)
{
    IntPoint clientPosition;
    if (!decoder.decode(clientPosition))
        return false;

    IntPoint globalPosition;
    if (!decoder.decode(globalPosition))
        return false;

    uint64_t sourceOperationMask;
    if (!decoder.decode(sourceOperationMask))
        return false;

    uint64_t flags;
    if (!decoder.decode(flags))
        return false;

    bool hasPlatformData;
    if (!decoder.decode(hasPlatformData))
        return false;

    RefPtr<DataObjectGtk> platformData;
    if (hasPlatformData) {
        if (!IPC::decode(decoder, platformData))
            return false;
    }

    dragData = DragData(platformData.leakRef(), clientPosition, globalPosition, static_cast<DragOperation>(sourceOperationMask),
                        static_cast<DragApplicationFlags>(flags));

    return true;
}
#endif // ENABLE(DRAG_SUPPORT)

static void encodeGKeyFile(Encoder& encoder, GKeyFile* keyFile)
{
    gsize dataSize;
    GUniquePtr<char> data(g_key_file_to_data(keyFile, &dataSize, 0));
    encoder << DataReference(reinterpret_cast<uint8_t*>(data.get()), dataSize);
}

static bool decodeGKeyFile(Decoder& decoder, GUniquePtr<GKeyFile>& keyFile)
{
    DataReference dataReference;
    if (!decoder.decode(dataReference))
        return false;

    if (!dataReference.size())
        return true;

    keyFile.reset(g_key_file_new());
    if (!g_key_file_load_from_data(keyFile.get(), reinterpret_cast<const gchar*>(dataReference.data()), dataReference.size(), G_KEY_FILE_NONE, 0)) {
        keyFile.reset();
        return false;
    }

    return true;
}

void encode(Encoder& encoder, GtkPrintSettings* printSettings)
{
    GUniquePtr<GKeyFile> keyFile(g_key_file_new());
    gtk_print_settings_to_key_file(printSettings, keyFile.get(), "Print Settings");
    encodeGKeyFile(encoder, keyFile.get());
}

bool decode(Decoder& decoder, GRefPtr<GtkPrintSettings>& printSettings)
{
    GUniquePtr<GKeyFile> keyFile;
    if (!decodeGKeyFile(decoder, keyFile))
        return false;

    printSettings = adoptGRef(gtk_print_settings_new());
    if (!keyFile)
        return true;

    if (!gtk_print_settings_load_key_file(printSettings.get(), keyFile.get(), "Print Settings", 0))
        printSettings = 0;

    return printSettings;
}

void encode(Encoder& encoder, GtkPageSetup* pageSetup)
{
    GUniquePtr<GKeyFile> keyFile(g_key_file_new());
    gtk_page_setup_to_key_file(pageSetup, keyFile.get(), "Page Setup");
    encodeGKeyFile(encoder, keyFile.get());
}

bool decode(Decoder& decoder, GRefPtr<GtkPageSetup>& pageSetup)
{
    GUniquePtr<GKeyFile> keyFile;
    if (!decodeGKeyFile(decoder, keyFile))
        return false;

    pageSetup = adoptGRef(gtk_page_setup_new());
    if (!keyFile)
        return true;

    if (!gtk_page_setup_load_key_file(pageSetup.get(), keyFile.get(), "Page Setup", 0))
        pageSetup = 0;

    return pageSetup;
}

}
