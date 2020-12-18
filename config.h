//
// Created by macskas on 12/10/20.
//

#ifndef NSCA_CONFIG_H
#define NSCA_CONFIG_H

#include <map>
#include <string>
#include <set>

typedef std::map<std::string,std::string>		configdata_t;


class config {
private:
    static config		*instance;
    configdata_t		data;
    std::string         pathConfig;
    std::set<std::string> valid_keywords;

public:
    config();

    void setConfigPath(std::string config);
    void read_config();
    int check_config();
    void set_allowed_config_keys();

    static config		*getInstance();
    int					Set(const std::string&, std::string);
    std::string			Get(const std::string&, std::string);
    long                GetInt(const std::string&, int);
};


#endif //NSCA_CONFIG_H
