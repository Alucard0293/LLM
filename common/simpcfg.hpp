#pragma once

/**
 * Provide a simple config file logic
 * It can consist of multiple config groups.
 * * the group name needs to start at the begining of the line.
 * Each group can inturn contain multiple config fields wrt that group.
 * * the group fields need to have 1 or more space at the begining of line.
 * 
 */

#include <map>
#include <string>
#include <vector>
#include <fstream>

#define SC_DEBUG
#define TEST_LOGIC
#ifdef TEST_LOGIC
#define LDBUG_LN(FMT, ...) printf(FMT"\n", __VA_ARGS__)
#define LERRR_LN(FMT, ...) printf(FMT"\n", __VA_ARGS__)
#define LWARN_LN(FMT, ...) printf(FMT"\n", __VA_ARGS__)
#else
#include "log.h"
#define LDBUG_LN LOGLN
#define LERRR_LN LOG_TEELN
#define LWARN_LN LOG_TEELN
#endif


std::string str_trim(std::string sin, std::string trimChars=" \t\n") {
    sin.erase(sin.find_last_not_of(trimChars)+1);
    sin.erase(0, sin.find_first_not_of(trimChars));
    return sin;
}


class SimpCfg {
private:
    std::map<std::string, std::map<std::string, std::string>> mapStrings = {};
    std::map<std::string, std::map<std::string, bool>> mapBools = {};
public:
    void set_string(const std::string &group, const std::string &key, const std::string &value) {
        auto gm = mapStrings[group];
        gm[key] = value;
        mapStrings[group] = gm;
        LDBUG_LN("DBUG:SC:%s:%s:%s:%s", __func__, group.c_str(), key.c_str(), value.c_str());
    }

    void set_bool(const std::string &group, const std::string &key, bool value) {
        auto gm = mapBools[group];
        gm[key] = value;
        mapBools[group] = gm;
        LDBUG_LN("DBUG:SC:%s:%s:%s:%d", __func__, group.c_str(), key.c_str(), value);
    }

    std::string get_string(const std::string &group, const std::string &key, const std::string &defaultValue) {
        auto gm = mapStrings[group];
#ifdef SC_DEBUG
        for(auto k: gm) {
            LDBUG_LN("DBUG:SC:%s:Iterate:%s:%s", __func__, k.first.c_str(), k.second.c_str());
        }
#endif
        if (gm.find(key) == gm.end()) {
            LWARN_LN("DBUG:SC:%s:%s:%s:%s[default]", __func__, group.c_str(), key.c_str(), defaultValue.c_str());
            return defaultValue;
        }
        auto value = gm[key];
        LDBUG_LN("DBUG:SC:%s:%s:%s:%s", __func__, group.c_str(), key.c_str(), value.c_str());
        return value;
    }

    bool get_bool(const std::string &group, const std::string &key, bool defaultValue) {
        auto gm = mapBools[group];
#ifdef SC_DEBUG
        for(auto k: gm) {
            LDBUG_LN("DBUG:SC:%s:Iterate:%s:%d", __func__, k.first.c_str(), k.second);
        }
#endif
        if (gm.find(key) == gm.end()) {
            LWARN_LN("DBUG:SC:%s:%s:%s:%d[default]", __func__, group.c_str(), key.c_str(), defaultValue);
            return defaultValue;
        }
        auto value = gm[key];
        LDBUG_LN("DBUG:SC:%s:%s:%s:%d", __func__, group.c_str(), key.c_str(), value);
        return value;
    }

    void load(const std::string &fname) {
        std::ifstream f {fname};
        if (!f) {
            LERRR_LN("ERRR:%s:%s:failed to load...", __func__, fname.c_str());
            throw std::runtime_error { "ERRR:SimpCfg:File not found" };
        } else {
            LDBUG_LN("DBUG:%s:%s", __func__, fname.c_str());
        }
        std::string group;
        int iLine = 0;
        while(!f.eof()) {
            iLine += 1;
            std::string curL;
            getline(f, curL);
            if (curL.empty()) {
                continue;
            }
            if (curL[0] == '#') {
                continue;
            }
            bool bGroup = !isspace(curL[0]);
            curL = str_trim(curL);
            if (bGroup) {
                group = curL;
                LDBUG_LN("DBUG:%s:group:%s", __func__, group.c_str());
                continue;
            }
            auto dPos = curL.find(':');
            if (dPos == std::string::npos) {
                LERRR_LN("ERRR:%s:%d:invalid key value line:%s", __func__, iLine, curL.c_str());
                throw std::runtime_error { "ERRR:SimpCfg:Invalid key value line" };
            }
            auto dEnd = curL.length() - dPos;
            if ((dPos == 0) || (dEnd < 2)) {
                LERRR_LN("ERRR:%s:%d:invalid key value line:%s", __func__, iLine, curL.c_str());
                throw std::runtime_error { "ERRR:SimpCfg:Invalid key value line" };
            }
            std::string key = curL.substr(0, dPos);
            key = str_trim(key);
            std::string value = curL.substr(dPos+1);
            value = str_trim(value);
            value = str_trim(value, ",");
            std::string vtype = "bool";
            if ((value == "true") || (value == "false")) {
                set_bool(group, key, value == "true" ? true : false);
            } else {
                vtype = "string";
                set_string(group, key, value);
            }
            //LDBUG_LN("DBUG:%s:kv:%s:%s:%s:%s", __func__, group.c_str(), key.c_str(), vtype.c_str(), value.c_str());
        }
    }

};


#ifdef TEST_LOGIC
int main(int argc, char **argv) {
    std::string fname {argv[1]};
    SimpCfg sc;
    sc.load(fname);
    sc.get_bool("testme", "key101b", false);
    sc.get_string("testme", "key101s", "Not found");
    sc.set_bool("testme", "key201b", true);
    sc.set_string("testme", "key201s", "hello world");
    sc.get_bool("testme", "key201b", false);
    sc.get_string("testme", "key201s", "Not found");
    sc.get_string("mistral", "system-prefix", "Not found");
    sc.get_string("\"mistral\"", "\"system-prefix\"", "Not found");
    return 0;
}
#endif
