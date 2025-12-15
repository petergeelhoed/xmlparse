#include <assert.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PAIRS 64 /* maximum unmatched speeds or flows */
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
    long data[MAX_PAIRS];
    size_t start;
    size_t end;
} long_ring_t;

static void lq_init(long_ring_t* queue)
{
    queue->start = 0;
    queue->end = 0;
}

static size_t lq_size(const long_ring_t* queue)
{
    return (queue->end >= queue->start) ? (queue->end - queue->start) : 0;
}

static int lq_push_back(long_ring_t* queue, long value)
{
    if (lq_size(queue) >= MAX_PAIRS)
    {
        return 0;
    }
    queue->data[queue->end++] = value;
    return 1;
}

static int lq_pop_front(long_ring_t* queue, long* out)
{
    if (lq_size(queue) == 0)
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
    double_ring_t speeds;
    long_ring_t flows;
    unsigned int idx;
} parser_state_t;

static void state_init(parser_state_t* str)
{
    str->site_id[0] = '\0';
    dq_init(&str->speeds);
    lq_init(&str->flows);
    str->idx = 1;
}

static void state_reset_block(parser_state_t* str)
{
    str->site_id[0] = '\0';
    str->speeds.start = 0;
    str->speeds.end = 0;
    str->flows.start = 0;
    str->flows.end = 0;
    str->idx = 1;
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

static int read_element_long(xmlTextReaderPtr reader, long* out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == NULL)
    {
        return 0;
    }

    const char* start = (char*)txt;
    char* end = NULL;
    errno = 0;
    const int decimal = 10;
    long value = strtol(start, &end, decimal);

    if (end == start || errno == ERANGE)
    {
        xmlFree(txt);
        return 0;
    }

    *out = value;
    xmlFree(txt);
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
    while (dq_size(&state->speeds) > 0 && lq_size(&state->flows) > 0)
    {
        double speed;
        long flow;
        if (!dq_pop_front(&state->speeds, &speed))
        {
            break;
        }
        if (!lq_pop_front(&state->flows, &flow))
        {
            break;
        }
        printf("%u %s %g %ld\n", state->idx++, site, speed, flow);
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

static int h_siteMeasurements(xmlTextReaderPtr reader, parser_state_t* state)
{
    (void)reader;
    state_reset_block(state);
    return 1;
}

static int h_measurementSiteReference(xmlTextReaderPtr reader,
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

static int h_speed(xmlTextReaderPtr reader, parser_state_t* state)
{
    double speed;
    if (read_element_double(reader, &speed))
    {
        if (!dq_push_back(&state->speeds, speed))
        {
            (void)fprintf(stderr,
                          "speed queue full (max %d), dropping value\n",
                          MAX_PAIRS);
        }
        else
        {
            state_flush_pairs(state);
        }
    }
    return 1;
}

static int h_vehicleFlowRate(xmlTextReaderPtr reader, parser_state_t* state)
{
    long rate;
    if (read_element_long(reader, &rate))
    {
        if (!lq_push_back(&state->flows, rate))
        {
            (void)fprintf(stderr,
                          "flow queue full (max %d), dropping value\n",
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
    {"siteMeasurements", h_siteMeasurements},
    {"measurementSiteReference", h_measurementSiteReference},
    {"speed", h_speed},
    {"vehicleFlowRate", h_vehicleFlowRate},
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
    if (name_is(localName, "siteMeasurements"))
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
