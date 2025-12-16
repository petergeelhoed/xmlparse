#include <assert.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PAIRS 64 /* maximum unmatched latitude or longitude */
#define MAX_TEXT 512 /* max length for site id / publicationTime strings */

static int name_is(const xmlChar* local, const char* key)
{
    return local != NULL && (xmlStrEqual(local, BAD_CAST key) != 0);
}

typedef struct
{
    double data[MAX_PAIRS];
    size_t start;
    size_t end;
} double_ring_t;

static void dq_init(double_ring_t* queue)
{
    queue->start = 0;
    queue->end = 0;
}

static size_t dq_size(const double_ring_t* queue)
{
    return (queue->end >= queue->start) ? (queue->end - queue->start) : 0;
}

static int dq_push_back(double_ring_t* queue, double value)
{
    if (dq_size(queue) >= MAX_PAIRS)
    {
        return 0;
    }
    queue->data[queue->end++] = value;
    return 1;
}

static int dq_pop_front(double_ring_t* queue, double* out)
{
    if (dq_size(queue) == 0)
    {
        return 0;
    }
    *out = queue->data[queue->start++];
    if (queue->start == queue->end)
    {
        queue->start = 0;
        queue->end = 0;
    }
    return 1;
}

typedef struct
{
    char site_id[MAX_TEXT];
    char date[MAX_TEXT];
    double_ring_t latitude;
    double_ring_t longitude;
} parser_state_t;

static void state_init(parser_state_t* str)
{
    str->site_id[0] = '\0';
    str->date[0] = '\0';
    dq_init(&str->latitude);
    dq_init(&str->longitude);
}

static void state_reset_block(parser_state_t* str)
{
    str->site_id[0] = '\0';
    str->date[0] = '\0';
    str->latitude.start = 0;
    str->latitude.end = 0;
    str->longitude.start = 0;
    str->longitude.end = 0;
}

static int read_element_text(xmlTextReaderPtr reader, char* buf, size_t bufsize)
{
    assert(bufsize > 0);
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == NULL)
    {
        if (buf && bufsize)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    size_t len = (size_t)xmlStrlen(txt);
    size_t copy = (len < (bufsize - 1)) ? len : (bufsize - 1);
    if (copy > 0)
    {
        memcpy(buf, txt, copy); // NOLINT
    }
    buf[copy] = '\0';
    xmlFree(txt);
    return 1;
}

static int read_attribute(xmlTextReaderPtr reader,
                          const char* name,
                          char* buf,
                          size_t bufsize)
{
    assert(bufsize > 0);
    xmlChar* val = xmlTextReaderGetAttribute(reader, BAD_CAST name);
    if (val == NULL)
    {
        if (buf && bufsize)
        {
            buf[0] = '\0';
        }
        return 0;
    }
    size_t len = (size_t)xmlStrlen(val);
    size_t copy = (len < (bufsize - 1)) ? len : (bufsize - 1);
    if (copy > 0)
    {
        memcpy(buf, val, copy); // NOLINT
    }
    buf[copy] = '\0';
    xmlFree(val);
    return 1;
}

static int read_element_double(xmlTextReaderPtr reader, double* out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == NULL)
    {
        return 0;
    }
    const char* start = (char*)txt;
    char* end = NULL;
    errno = 0;
    double value = strtod(start, &end);
    if (end == start || errno == ERANGE)
    {
        xmlFree(txt);
        return 0;
    }
    *out = value;
    xmlFree(txt);
    return 1;
}

static void state_flush_pairs(parser_state_t* state)
{
    const char* site = (state->site_id[0]) ? state->site_id : "(unknown_site)";
    const char* date = (state->date[0]) ? state->date : "(unknown_date)";
    while (dq_size(&state->latitude) > 0 && dq_size(&state->longitude) > 0)
    {
        double lat;
        double longi;
        if (!dq_pop_front(&state->latitude, &lat))
        {
            break;
        }
        if (!dq_pop_front(&state->longitude, &longi))
        {
            break;
        }
        printf("%s %s %g %g\n", site, date, lat, longi);
    }
}

typedef int (*element_handler_t)(xmlTextReaderPtr, parser_state_t*);

static int h_publicationTime(xmlTextReaderPtr reader, parser_state_t* state)
{
    (void)state;
    char buf[MAX_TEXT];
    if (read_element_text(reader, buf, sizeof buf))
    {
        puts(buf);
    }
    return 1;
}

static int h_measurementSiteRecordVersionTime(xmlTextReaderPtr reader,
                                              parser_state_t* state)
{
    char buf[MAX_TEXT];
    if (read_element_text(reader, buf, sizeof buf))
    {
        strncpy(state->date, buf, sizeof state->date); // NOLINT
        state->date[sizeof state->date - 1] = '\0';
    }
    else
    {
        state->date[0] = '\0';
    }
    return 1;
}

static int h_siteMeasurements(xmlTextReaderPtr reader, parser_state_t* state)
{
    (void)reader;
    state_reset_block(state);
    return 1;
}

static int h_measurementSiteRecord(xmlTextReaderPtr reader,
                                   parser_state_t* state)
{
    char buf[MAX_TEXT];
    if (read_attribute(reader, "id", buf, sizeof buf))
    {
        strncpy(state->site_id, buf, sizeof state->site_id); // NOLINT
        state->site_id[sizeof state->site_id - 1] = '\0';
    }
    else
    {
        state->site_id[0] = '\0';
    }
    return 1;
}

static int h_latitude(xmlTextReaderPtr reader, parser_state_t* state)
{
    double lat;
    if (read_element_double(reader, &lat))
    {
        if (!dq_push_back(&state->latitude, lat))
        {
            (void)fprintf(
                stderr, "lat queue full (max %d), dropping value\n", MAX_PAIRS);
        }
        else
        {
            state_flush_pairs(state);
        }
    }
    return 1;
}

static int h_longitude(xmlTextReaderPtr reader, parser_state_t* state)
{
    double rate;
    if (read_element_double(reader, &rate))
    {
        if (!dq_push_back(&state->longitude, rate))
        {
            (void)fprintf(stderr,
                          "longitude queue full (max %d), dropping value\n",
                          MAX_PAIRS);
        }
        else
        {
            state_flush_pairs(state);
        }
    }
    return 1;
}

struct element_dispatch
{
    const char* name;
    element_handler_t fn;
};

static const struct element_dispatch DISPATCH[] = {
    {"publicationTime", h_publicationTime},
    {"measurementSiteTable", h_siteMeasurements},
    {"measurementSiteRecord", h_measurementSiteRecord},
    {"measurementSiteRecordVersionTime", h_measurementSiteRecordVersionTime},
    {"latitude", h_latitude},
    {"longitude", h_longitude},
};

static int handle_start_element(xmlTextReaderPtr reader,
                                const xmlChar* localName,
                                parser_state_t* state)
{
    for (size_t i = 0; i < sizeof(DISPATCH) / sizeof(DISPATCH[0]); ++i)
    {
        if (name_is(localName, DISPATCH[i].name))
        {
            return DISPATCH[i].fn(reader, state);
        }
    }
    return 0;
}

static int handle_end_element(const xmlChar* localName, parser_state_t* state)
{
    if (name_is(localName, "measurementSiteTable"))
    {
        state_flush_pairs(state);
        state_reset_block(state);
        return 1;
    }
    return 0;
}

static void process_reader(xmlTextReaderPtr reader, parser_state_t* state)
{
    int ret;
    while ((ret = xmlTextReaderRead(reader)) == 1)
    {
        int nodeType = xmlTextReaderNodeType(reader);
        const xmlChar* localName = xmlTextReaderConstLocalName(reader);
        if (nodeType == XML_READER_TYPE_ELEMENT)
        {
            (void)handle_start_element(reader, localName, state);
        }
        else if (nodeType == XML_READER_TYPE_END_ELEMENT)
        {
            (void)handle_end_element(localName, state);
        }
    }
    if (ret == -1)
    {
        (void)fprintf(stderr, "XML read error encountered\n");
    }
}

int main(void)
{
    int options = XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NOBLANKS;
    xmlTextReaderPtr reader =
        xmlReaderForFd(STDIN_FILENO, "stdin", NULL, options);
    if (reader == NULL)
    {
        (void)fprintf(stderr, "Failed to create XML reader\n");
        return 1;
    }

    parser_state_t state;
    state_init(&state);
    process_reader(reader, &state);
    xmlFreeTextReader(reader);
    xmlCleanupParser();
    return 0;
}
