#ifndef __PINUTIL_H__
#define __PINUTIL_H__

#include <stack>
#include <algorithm>
#include <iostream>

#include "pin.H"
#include "INIReader.h"

namespace PIMProf {
/* ===================================================================== */
/* Typedefs and constants */
/* ===================================================================== */
typedef UINT32 CACHE_STATS;
typedef FLT64 COST;
typedef UINT32 BBLID;


static const UINT32 MAX_COST_SITE = 2;
enum CostSite {
    CPU = 0,
    PIM = 1,
    INVALID = 0x3fffffff // a placeholder that does not count as a cost site
};
static const std::string CostSiteName[MAX_COST_SITE] = { "CPU", "PIM" };

static const BBLID GLOBALBBLID = 0x7FFFFFFF;

enum ACCESS_TYPE
{
    ACCESS_TYPE_LOAD,
    ACCESS_TYPE_STORE,
    ACCESS_TYPE_NUM
};

/* ===================================================================== */
/* BBLScope */
/* ===================================================================== */
class BBLScope {
  private:
    std::stack<BBLID> bblidstack;
  public:
    inline BBLScope()
    {
        std::cerr << "BBLScope allocated" << std::endl;
        // push a fake global bblid
        bblidstack.push(GLOBALBBLID);
    }
    inline void push(BBLID bblid)
    {
        bblidstack.push(bblid);
    }
    inline void pop()
    {
        bblidstack.pop();
    }
    inline BBLID top()
    {
        return bblidstack.top();
    }

    inline ~BBLScope()
    {
        std::cerr << "BBLScope deallocated" << std::endl;
    }
};

/* ===================================================================== */
/* Output Formatting */
/* ===================================================================== */

const string REDCOLOR = "\033[0;31m";
const string GREENCOLOR = "\033[0;32m";
const string YELLOWCOLOR = "\033[0;33m";
const string NOCOLOR = "\033[0m";

inline std::ostream &errormsg()
{
    std::cerr << REDCOLOR << "## PIMProf ERROR: " << NOCOLOR;
    return std::cerr;
}

inline std::ostream &warningmsg()
{
    std::cerr << YELLOWCOLOR << "## PIMProf WARNING: " << NOCOLOR;
    return std::cerr;
}

inline std::ostream &infomsg()
{
    std::cout << GREENCOLOR << "## PIMProf INFO: " << NOCOLOR;
    return std::cout;
}

/* ===================================================================== */
/* CommandLineParser */
/* ===================================================================== */

class CommandLineParser {
  private:
    std::string _rootdir;
    std::string _configfile;
    std::string _outputfile;
    std::string _controlflowfile;
    
  public:
    void initialize(int argc, char *argv[]);

    std::string rootdir() { return _rootdir; }
    std::string configfile() { return _configfile; }
    std::string outputfile() { return _outputfile; }
    std::string controlflowfile() { return _controlflowfile; }

    inline int Usage(std::ostream &out) {
        out << "Invalid argument."
            << std::endl
            << KNOB_BASE::StringKnobSummary()
            << std::endl;
        return -1;
    }

};

/* ===================================================================== */
/* ConfigReader */
/* ===================================================================== */

class ConfigReader: public INIReader {
  private:
    std::string filename;
  public:
    inline ConfigReader() {}

    inline ConfigReader(std::string f)
        : INIReader(f), filename(f)
    {
        INT32 error = ParseError();
        if (error == -1) {
            errormsg() << ".ini file open error." << std::endl;
        }
        else if (error == -2) {
            errormsg() << ".ini file memory allocation for parsing error." << std::endl;
        } 
        
        else if (error > 0) {
            errormsg() << ".ini file parsing failure on line "
                << error 
                << "." << std::endl;
        }
        if (error)
            errormsg() << "Filename: " << filename << std::endl;
    }
};

} // namespace PIMProf

#endif // __PINUTIL_H__