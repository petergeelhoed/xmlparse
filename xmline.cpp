
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <libxml/xmlreader.h>
#include <libxml/xmlstring.h>
#include <string>

// Compare libxml2 xmlChar* local name with a C string
static inline bool nameIs(const xmlChar* local, const char* key)
{
    // NOLINTBEGIN[cppcoreguidelines-pro-type-reinterpret-cast]
    return local != nullptr &&
           (xmlStrEqual(local, reinterpret_cast<const xmlChar*>(key)) != 0);
    // NOLINTEND[cppcoreguidelines-pro-type-reinterpret-cast]
}

// Read element text as long (vehicleFlowRate) without any casts
static inline bool readElementLong(xmlTextReaderPtr reader, long& out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == nullptr)
    {
        return false;
    }

    const auto len = static_cast<size_t>(xmlStrlen(txt));
    std::string str(len, '\0');
    std::copy_n(txt, len, str.begin());
    xmlFree(txt);

    char* end = nullptr;
    const unsigned int decimal = 10;
    long value = std::strtol(str.c_str(), &end, decimal);
    if (end == str.c_str())
    {
        return false;
    }
    out = value;
    return true;
}

// Read element text as double (speed: supports floats)
static inline bool readElementDouble(xmlTextReaderPtr reader, double& out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == nullptr)
    {
        return false;
    }

    const auto len = static_cast<size_t>(xmlStrlen(txt));
    std::string str(len, '\0');
    std::copy_n(txt, len, str.begin());
    xmlFree(txt);

    char* end = nullptr;
    double value = std::strtod(str.c_str(), &end);
    if (end == str.c_str())
    {
        return false;
    }
    out = value;
    return true;
}

static inline bool readElementString(xmlTextReaderPtr reader, std::string& out)
{
    xmlChar* txt = xmlTextReaderReadString(reader);
    if (txt == nullptr)
    {
        out.clear();
        return false;
    }

    const auto len = static_cast<size_t>(xmlStrlen(txt));
    out.resize(len);
    if (len > 0)
    {
        std::copy_n(txt, len, out.begin());
    }

    xmlFree(txt);
    return true;
}

static inline bool
readAttribute(xmlTextReaderPtr reader, const char* name, std::string& out)
{
    // NOLINTBEGIN[cppcoreguidelines-pro-type-reinterpret-cast]
    xmlChar* val = xmlTextReaderGetAttribute(
        reader, reinterpret_cast<const xmlChar*>(name));
    // NOLINTEND[cppcoreguidelines-pro-type-reinterpret-cast]
    if (val == nullptr)
    {
        out.clear();
        return false;
    }
    const auto len = static_cast<size_t>(xmlStrlen(val));
    out.resize(len);
    if (len > 0)
    {
        std::copy_n(val, len, out.begin());
    }
    xmlFree(val);
    return true;
}

// Configure stdout buffering (8 MiB). Return true on success.
static inline bool configureStdoutBuffering()
{
    constexpr std::size_t eightMB = 8 * 1024 * 1024;
    return std::setvbuf(stdout, nullptr, _IOFBF, eightMB) == 0;
}

// Build libxml2 options as unsigned to satisfy hicpp-signed-bitwise
static inline int xmlReaderOptions()
{
    constexpr unsigned int optsUnsigned =
        static_cast<unsigned int>(XML_PARSE_NOERROR) |
        static_cast<unsigned int>(XML_PARSE_NOWARNING) |
        static_cast<unsigned int>(XML_PARSE_NOBLANKS);
    return static_cast<int>(optsUnsigned);
}

struct ParserState
{
    std::string siteId;
    std::deque<double> speeds;
    std::deque<long> flows;
    unsigned int idx = 1;

    void resetBlock()
    {
        siteId.clear();
        speeds.clear();
        flows.clear();
        idx = 1;
    }

    void flushPairs()
    {
        const char* site = siteId.empty() ? "(unknown_site)" : siteId.c_str();
        while (!speeds.empty() && !flows.empty())
        {
            double speed = speeds.front();
            speeds.pop_front();
            long flow = flows.front();
            flows.pop_front();
            std::cout << idx++ << ' ' << site << ' ' << std::defaultfloat
                      << speed << ' ' << flow << '\n';
        }
    }
};

static inline bool handleStartElement(xmlTextReaderPtr reader,
                                      const xmlChar* localName,
                                      ParserState& state)
{
    if (nameIs(localName, "publicationTime"))
    {
        std::string time;
        if (readElementString(reader, time))
        {
            std::cout << time << '\n';
        }
        return true;
    }

    if (nameIs(localName, "siteMeasurements"))
    {
        state.resetBlock();
        return true;
    }

    if (nameIs(localName, "measurementSiteReference"))
    {
        (void)readAttribute(reader, "id", state.siteId);
        return true;
    }

    if (nameIs(localName, "speed"))
    {
        double speed = NAN;
        if (readElementDouble(reader, speed))
        {
            state.speeds.push_back(speed);
            state.flushPairs();
        }
        return true;
    }

    if (nameIs(localName, "vehicleFlowRate"))
    {
        long rate = 0;
        if (readElementLong(reader, rate))
        {
            state.flows.push_back(rate);
            state.flushPairs();
        }
        return true;
    }

    return false;
}

static inline bool handleEndElement(const xmlChar* localName,
                                    ParserState& state)
{
    if (nameIs(localName, "siteMeasurements"))
    {
        state.flushPairs(); // flush remaining matched pairs for this block
        state.resetBlock(); // drop leftovers without match
        return true;
    }
    return false;
}

static inline void processReader(xmlTextReaderPtr reader, ParserState& state)
{
    while (xmlTextReaderRead(reader) == 1)
    {
        const int nodeType = xmlTextReaderNodeType(reader);
        const xmlChar* localName = xmlTextReaderConstLocalName(reader);

        if (nodeType == XML_READER_TYPE_ELEMENT)
        {
            (void)handleStartElement(reader, localName, state);
        }
        else if (nodeType == XML_READER_TYPE_END_ELEMENT)
        {
            (void)handleEndElement(localName, state);
        }
    }
}

int main()
{
    if (!configureStdoutBuffering())
    {
        std::cerr << "Failed to create outstream buffer.\n";
        return 1;
    }

    const int options = xmlReaderOptions();

    // Create a pull reader directly from stdin
    xmlTextReaderPtr reader = xmlReaderForFd(fileno(stdin),
                                             "stdin",
                                             nullptr, // autodetect encoding
                                             options);

    if (reader == nullptr)
    {
        std::cerr << "Failed to create XML reader.\n";
        return 1;
    }

    ParserState state;
    processReader(reader, state);

    xmlFreeTextReader(reader);
    xmlCleanupParser();
    return 0;
}
