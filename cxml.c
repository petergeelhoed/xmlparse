/* xmline.c
 *
 * Rewritten from the C++ xmline.cpp into plain C using libxml2'str
 * xmlTextReader.
 *
 * Compile:
 *   gcc -std=c11 -Wall -Wextra xmline.c -o xmline `xml2-config --cflags --libs`
 *
 * Usage:
 *   xmline < input.xml
 */

#include <assert.h>
#include <errno.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Compare libxml2 xmlChar* local name with a C string */
static int name_is(const xmlChar* local, const char* key)
{
    return local != NULL && (xmlStrEqual(local, BAD_CAST key) != 0);
}

/* Simple dynamic queue for doubles */
typedef struct
{
    double* data;
    size_t capacity;
    size_t start; /* index of first element */
    size_t end;   /* index one past last element */
} double_queue_t;

static void dq_init(double_queue_t* queue)
{
    queue->data = NULL;
    queue->capacity = 0;
    queue->start = queue->end = 0;
}

static void dq_free(double_queue_t* queue)
{
    free(queue->data);
    queue->data = NULL;
    queue->capacity = 0;
    queue->start = queue->end = 0;
}

static size_t dq_size(const double_queue_t* queue)
{
    return (queue->end >= queue->start) ? (queue->end - queue->start) : 0;
}

static int dq_ensure_capacity(double_queue_t* queue, size_t additional)
{
    assert(queue);

    const size_t size = dq_size(queue);

    // --- Overflow-safe computation of needed slots ---
    // We need 'size + additional' slots available in total.
    if (additional > SIZE_MAX - size)
    {
        // size + additional would overflow
        return 0;
    }
    const size_t needed = size + additional;

    // If capacity already sufficient, we're done.
    if (needed <= queue->capacity)
    {
        return 1;
    }

    // --- Compute new capacity with geometric growth, guarding overflow ---
    const size_t initial_capacity = 16;
    size_t newcap = (queue->capacity ? queue->capacity : initial_capacity);

    // Grow at least until 'needed', doubling when possible.
    while (newcap < needed)
    {
        // Prevent overflow when doubling
        if (newcap > SIZE_MAX / 2)
        {
            newcap = needed; // fall back to exact needed (last safe step)
            break;
        }
        newcap *= 2;
    }

    // --- Allocate or grow the buffer ---
    // Note: realloc(NULL, n) behaves like malloc(n).
    double* newdata = (double*)realloc(queue->data, newcap * sizeof *newdata);
    if (!newdata)
    {
        // Allocation failed; keep the old buffer intact
        return 0;
    }

    // Update pointer and capacity first (safe now that we have storage).
    queue->data = newdata;
    queue->capacity = newcap;

    // --- Normalize the active slice to begin at index 0 ---
    // If there are elements and the slice doesn't start at 0, shift it.
    if (size > 0 && queue->start != 0)
    {
        // NOLINTNEXTLINE
        memmove(queue->data,
                queue->data + queue->start,
                size * sizeof *queue->data);
    }

    // Reset indices to represent the normalized slice: [0, size)
    queue->start = 0;
    queue->end = size;

    return 1;
}

static int dq_push_back(double_queue_t* queue, double value)
{
    if (!dq_ensure_capacity(queue, 1))
    {
        return 0;
    }
    queue->data[queue->end++] = value;
    return 1;
}

static int dq_pop_front(double_queue_t* queue, double* out)
{
    if (dq_size(queue) == 0)
    {
        return 0;
    }
    *out = queue->data[queue->start++];
    if (queue->start == queue->end)
    {
        queue->start = queue->end = 0; /* reset to avoid unbounded indices */
    }
    return 1;
}

/* Same dynamic queue for long integers */
typedef struct
{
    long* data;
    size_t capacity;
    size_t start;
    size_t end;
} long_queue_t;

static void lq_init(long_queue_t* queue)
{
    queue->data = NULL;
    queue->capacity = 0;
    queue->start = queue->end = 0;
}

static void lq_free(long_queue_t* queue)
{
    free(queue->data);
    queue->data = NULL;
    queue->capacity = 0;
    queue->start = queue->end = 0;
}

static size_t lq_size(const long_queue_t* queue)
{
    return (queue->end >= queue->start) ? (queue->end - queue->start) : 0;
}

static int lq_ensure_capacity(long_queue_t* queue, size_t additional)
{
    assert(queue);

    const size_t size = lq_size(queue);

    // --- Overflow-safe computation of needed slots ---
    // We need 'size + additional' slots available in total.
    if (additional > SIZE_MAX - size)
    {
        // size + additional would overflow
        return 0;
    }
    const size_t needed = size + additional;

    // If capacity already sufficient, we're done.
    if (needed <= queue->capacity)
    {
        return 1;
    }

    // --- Compute new capacity with geometric growth, guarding overflow ---
    const size_t initial_capacity = 16;
    size_t newcap = (queue->capacity ? queue->capacity : initial_capacity);

    // Grow at least until 'needed', doubling when possible.
    while (newcap < needed)
    {
        // Prevent overflow when doubling
        if (newcap > SIZE_MAX / 2)
        {
            newcap = needed; // fall back to exact needed (last safe step)
            break;
        }
        newcap *= 2;
    }

    // --- Allocate or grow the buffer ---
    // Note: realloc(NULL, n) behaves like malloc(n).
    long* newdata = (long*)realloc(queue->data, newcap * sizeof *newdata);
    if (!newdata)
    {
        // Allocation failed; keep the old buffer intact
        return 0;
    }

    // Update pointer and capacity first (safe now that we have storage).
    queue->data = newdata;
    queue->capacity = newcap;

    // --- Normalize the active slice to begin at index 0 ---
    // If there are elements and the slice doesn't start at 0, shift it.
    if (size > 0 && queue->start != 0)
    {
        // NOLINTNEXTLINE
        memmove(queue->data,
                queue->data + queue->start,
                size * sizeof *queue->data);
    }

    // Reset indices to represent the normalized slice: [0, size)
    queue->start = 0;
    queue->end = size;

    return 1;
}

static int lq_push_back(long_queue_t* queue, long value)
{
    if (!lq_ensure_capacity(queue, 1))
    {
        return 0;
    }
    queue->data[queue->end++] = value;
    return 1;
}

static int lq_pop_front(long_queue_t* queue, long* out)
{
    if (lq_size(queue) == 0)
    {
        return 0;
    }
    *out = queue->data[queue->start++];
    if (queue->start == queue->end)
    {
        queue->start = queue->end = 0;
    }
    return 1;
}

/* Parser state */
typedef struct
{
    char* site_id; /* allocated; may be NULL */
    double_queue_t speeds;
    long_queue_t flows;
    unsigned int idx; /* 1-based index within siteMeasurements block */
} parser_state_t;

static void state_init(parser_state_t* state)
{
    state->site_id = NULL;
    dq_init(&state->speeds);
    lq_init(&state->flows);
    state->idx = 1;
}

static void state_free(parser_state_t* state)
{
    free(state->site_id);
    dq_free(&state->speeds);
    lq_free(&state->flows);
    state->site_id = NULL;
    state->idx = 1;
}

static void state_reset_block(parser_state_t* state)
{
    free(state->site_id);
    state->site_id = NULL;
    /* clear queues but keep capacity */
    state->speeds.start = state->speeds.end = 0;
    state->flows.start = state->flows.end = 0;
    state->idx = 1;
}

/* Helper: read xmlTextReaderReadString() into a newly malloc'd NUL-terminated C
 * string. Caller must free(*out) with free(). Returns 1 on success, 0 on
 * failure (and sets *out to NULL).
 */
static int read_element_string(xmlTextReaderPtr reader, char** out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == NULL)
    {
        *out = NULL;
        return 0;
    }
    int ilen = xmlStrlen(txt);
    if (ilen < 0)
    {
        ilen = 0;
    }
    size_t len = (size_t)ilen;
    char* str = (char*)malloc(len + 1);
    if (!str)
    {
        xmlFree(txt);
        *out = NULL;
        return 0;
    }
    if (len > 0)
    {
        // NOLINTNEXTLINE[clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling]
        memcpy(str, txt, len);
    }
    str[len] = '\0';
    xmlFree(txt);
    *out = str;
    return 1;
}

/* Helper: read attribute value into newly malloc'd string (caller frees). */
static int read_attribute(xmlTextReaderPtr reader, const char* name, char** out)
{
    xmlChar* val = xmlTextReaderGetAttribute(reader, BAD_CAST name);
    if (val == NULL)
    {
        *out = NULL;
        return 0;
    }
    int ilen = xmlStrlen(val);
    if (ilen < 0)
    {
        ilen = 0;
    }
    size_t len = (size_t)ilen;
    char* str = (char*)malloc(len + 1);
    if (!str)
    {
        xmlFree(val);
        *out = NULL;
        return 0;
    }
    if (len > 0)
    {
        // NOLINTNEXTLINE
        memcpy(str, val, len);
    }
    str[len] = '\0';
    xmlFree(val);
    *out = str;
    return 1;
}

/* read element text and parse as long */
static int read_element_long(xmlTextReaderPtr reader, long* out)
{
    char* str = NULL;
    if (!read_element_string(reader, &str))
    {
        return 0;
    }

    if (str == NULL)
    {
        return 0;
    }

    char* end = NULL;
    errno = 0;
    const int decimal = 10;
    long val = strtol(str, &end, decimal);
    if (end == str)
    {
        free(str);
        return 0;
    }
    if (errno == ERANGE)
    {
        free(str);
        return 0;
    }
    *out = val;
    free(str);
    return 1;
}

/* read element text and parse as double */
static int read_element_double(xmlTextReaderPtr reader, double* out)
{
    char* str = NULL;
    if (!read_element_string(reader, &str))
    {
        return 0;
    }

    if (str == NULL)
    {
        return 0;
    }

    char* end = NULL;
    errno = 0;
    double val = strtod(str, &end);
    if (end == str)
    {
        free(str);
        return 0;
    }
    if (errno == ERANGE)
    {
        free(str);
        return 0;
    }
    *out = val;
    free(str);
    return 1;
}

/* Flush pairs: while both speeds and flows have elements, pop and print */
static void state_flush_pairs(parser_state_t* state)
{
    const char* site = (state->site_id && state->site_id[0]) ? state->site_id
                                                             : "(unknown_site)";
    while (dq_size(&state->speeds) > 0 && lq_size(&state->flows) > 0)
    {
        double speed = 0.0;
        long flow = -1;
        int ok1 = dq_pop_front(&state->speeds, &speed);
        int ok2 = lq_pop_front(&state->flows, &flow);
        if (!ok1 || !ok2)
        {
            break;
        }
        printf("%u %s %g %ld\n", state->idx++, site, speed, flow);
    }
}

/* Handle a start element */
static int handle_start_element(xmlTextReaderPtr reader,
                                const xmlChar* localName,
                                parser_state_t* state)
{
    if (name_is(localName, "publicationTime"))
    {
        char* time = NULL;
        if (read_element_string(reader, &time))
        {
            puts(time);
        }
        free(time);
        return 1;
    }

    if (name_is(localName, "siteMeasurements"))
    {
        state_reset_block(state);
        return 1;
    }

    if (name_is(localName, "measurementSiteReference"))
    {
        char* siteid = NULL;
        if (read_attribute(reader, "id", &siteid))
        {
            free(state->site_id);
            state->site_id = siteid;
        }
        return 1;
    }

    if (name_is(localName, "speed"))
    {
        double speed = 0.0;
        if (read_element_double(reader, &speed))
        {
            if (!dq_push_back(&state->speeds, speed))
            {
                /* allocation failure - print error and continue */
                (void)fprintf(stderr, "Out of memory pushing speed\n");
            }
            else
            {
                state_flush_pairs(state);
            }
        }
        return 1;
    }

    if (name_is(localName, "vehicleFlowRate"))
    {
        long rate = 0;
        if (read_element_long(reader, &rate))
        {
            if (!lq_push_back(&state->flows, rate))
            {
                (void)fprintf(stderr, "Out of memory pushing flow\n");
            }
            else
            {
                state_flush_pairs(state);
            }
        }
        return 1;
    }

    return 0;
}

/* Handle end element */
static int handle_end_element(const xmlChar* localName, parser_state_t* state)
{
    if (name_is(localName, "siteMeasurements"))
    {
        /* flush remaining matched pairs for this block, then drop leftovers */
        state_flush_pairs(state);
        state_reset_block(state);
        return 1;
    }
    return 0;
}

/* Main reader loop */
static void process_reader(xmlTextReaderPtr reader, parser_state_t* state)
{
    int ret = -1;
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

    /* create reader for stdin */
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

    state_free(&state);
    return 0;
}
