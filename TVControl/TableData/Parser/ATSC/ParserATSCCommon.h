#ifndef ATSC_COMMON_H_
#define ATSC_COMMON_H_

#include <glib.h>
#include <gst/gst.h>
#include <gst/mpegts/mpegts.h>

#define GPS_EPOCH 315964800

static time_t atsctime_to_unixtime(guint32 atsc)
{
    return atsc + GPS_EPOCH;
}

static void _gst_mpegts_atsc_string_segment_free(GstMpegtsAtscStringSegment* seg)
{
    g_free(seg->cached_string);
    g_slice_free(GstMpegtsAtscStringSegment, seg);
}

static void _gst_mpegts_atsc_mult_string_free(GstMpegtsAtscMultString* mstring)
{
    g_ptr_array_unref(mstring->segments);
    g_slice_free(GstMpegtsAtscMultString, mstring);
}

bool gst_mpegts_descriptor_parse_atsc_caption_service_idx(const GstMpegtsDescriptor* descriptor, guint idx, std::string& language)
{
    guint8* data;

    if (descriptor->length / 6 <= idx)
        return false;

    data = (guint8*)descriptor->data + 3 + idx * 6;
    language = std::string((char*)data, 3);
    return true;
}


bool gst_mpegts_descriptor_parse_atsc_service_location_idx(const GstMpegtsDescriptor* descriptor, guint idx, guint8* stream_type, std::string& language, guint16* elementary_pid)
{
    guint8* data;

    if (descriptor->length / 6 <= idx)
        return false;

    data = (guint8*)descriptor->data + 5 + idx * 6;
    if (stream_type)
        *stream_type = data[0];

    data += 1;
    *elementary_pid = GST_READ_UINT16_BE(data) & 0x01ff;

    data += 2;
    language = std::string((char*)data, 3);

    return true;
}


static GPtrArray* _parse_atsc_mult_string(guint8* data, guint datasize)
{
    guint8 num_strings;
    GPtrArray* res = nullptr;
    guint8* end = data + datasize;

    if (datasize > 0) {
        /* 1 is the minimum entry size, so no need to check here */
        num_strings = GST_READ_UINT8(data);
        data += 1;

        res = g_ptr_array_new_full(num_strings, (GDestroyNotify)_gst_mpegts_atsc_mult_string_free);

        for (gint i = 0; i < num_strings; i++) {
            GstMpegtsAtscMultString* mstring;
            guint8 num_segments;

            mstring = g_slice_new0(GstMpegtsAtscMultString);
            g_ptr_array_add(res, mstring);
            mstring->segments = g_ptr_array_new_full(num_strings, (GDestroyNotify)_gst_mpegts_atsc_string_segment_free);

            /* each entry needs at least 4 bytes (lang code and segments number) */
            if (end - data < 4) {
                GST_WARNING ("Data too short for multstring parsing %d",
                        (gint) (end - data));
                goto error;
            }

            mstring->iso_639_langcode[0] = GST_READ_UINT8(data);
            data += 1;
            mstring->iso_639_langcode[1] = GST_READ_UINT8(data);
            data += 1;
            mstring->iso_639_langcode[2] = GST_READ_UINT8(data);
            data += 1;
            num_segments = GST_READ_UINT8 (data);
            data += 1;

            for (guint j = 0; j < num_segments; j++) {
                GstMpegtsAtscStringSegment* seg;

                seg = g_slice_new0(GstMpegtsAtscStringSegment);
                g_ptr_array_add(mstring->segments, seg);

                /* each entry needs at least 3 bytes */
                if (end - data < 3) {
                    GST_WARNING ("Data too short for multstring parsing %d", datasize);
                    goto error;
                }

                seg->compression_type = GST_READ_UINT8(data);
                data += 1;
                seg->mode = GST_READ_UINT8(data);
                data += 1;
                seg->compressed_data_size = GST_READ_UINT8(data);
                data += 1;

                if (end - data < seg->compressed_data_size) {
                    GST_WARNING ("Data too short for multstring parsing %d", datasize);
                    goto error;
                }

                if (seg->compressed_data_size)
                    seg->compressed_data = data;
                data += seg->compressed_data_size;
            }

        }
    }
    return res;

error:
    if (res)
        g_ptr_array_unref(res);
    return nullptr;
}
#endif
