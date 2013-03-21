/*Copyright (C) Bruno Garnier
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
* Author:
* Bruno Garnier */ 

#include<arpa/inet.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include <arpa/inet.h>

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include "curl/curl.h"

#include <fstream>
#include <iostream>
#include <boost/regex.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <ctime>

//using namespace std;
using std::cout;
using std::endl;
using std::string;

using namespace boost::program_options;
namespace po = boost::program_options;

const int ZIBASE_CMD_HOST_REGISTERING = 13;
const int ZIBASE_CMD_HOST_UNREGISTERING = 22;
const int ZIBASE_CMD_RF_FRAME_SENDING = 11;
const int ZIBASE_CMD_SCENARIO_LAUNCHING = 11;
const int ZIBASE_CMD_SET_EVENT = 11;

static string buffer;

string zibase_address;
int zibase_port = 49999;
string bind_address;
int bind_port = 49998;

bool opt_log;
bool opt_verbose;
string log_path = "/tmp/zibase.log";

bool opt_post;
string post_url;
string post_auth;

enum ZAPI_protocol {
   	PRESET = 0,
 	VISONIC433 = 1,
  	VISONIC868 = 2,
  	CHACON = 3,
  	DOMIA = 4,
  	X10 = 5,
  	ZWAVE = 6,
  	RFS10 = 7,
  	X2D433 = 8,
  	X2D433ALRM = 8,
  	X2D868 = 9,
  	X2D868ALRM = 9,
  	X2D868INSH = 10,
  	X2D868PIWI = 11,
  	X2D868BOAC = 12
};

enum ZAPI_virtual_probe {
    OREGON = 17,
    OWL = 20
}; 
 
enum ZAPI_action {
    OFF = 0,
    ON = 1,
    DIM_BRIGHT = 2,
    ALL_LIGHTS_ON = 4,
    ALL_LIGHTS_OFF = 5,
    ALL_OFF = 6,
    ASSOC = 7
};

struct ZAPI_packet_header{

    ZAPI_packet_header(uint16_t cmd=0, uint32_t p1=0, uint32_t p2=0, uint32_t p3=0, uint32_t p4=0) :
        command(cmd),
        param1(p1),
        param2(p2),
        param3(p3),
        param4(p4),
        my_count(0),
        your_count(0)
    {
        strncpy((char*)header, "ZSIG", sizeof(header));

    }

    unsigned char header[4];
    uint16_t command; // command number
    unsigned char reserved1[16];
    unsigned char zibase_id[16];
    unsigned char reserved2[12];
    uint32_t param1; // generic parameters for commands
    uint32_t param2; // generic parameters for commands
    uint32_t param3; // generic parameters for commands
    uint32_t param4; // generic parameters for commands
    uint16_t my_count; // incr.counter at each sent packet (not incr. in burst of same packets)
    uint16_t your_count; // last counter value received by the other side

    ///network to host
    void ntoh()
    {
        command = ntohs(command);
        param1 = ntohl(param1);
        param2 = ntohl(param2);
        param3 = ntohl(param3);
        my_count = ntohs(my_count);
        your_count = ntohs(your_count);
    }
    ///host to network
    void hton()
    {
        command = htons(command);
        param1 = htonl(param1);
        param2 = htonl(param2);
        param3 = htonl(param3);
        my_count = htons(my_count);
        your_count = htons(your_count);
    }
} __attribute__((packed));


struct ZAPI_packet_receiving : public ZAPI_packet_header {
    char text_field[400];
} __attribute__((packed));

void error( const char * msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// This is the writer call back function used by curl
static int writer(char *data, size_t size, size_t nmemb,
        std::string *buffer)
{
    // What we will return
    int result = 0;

    // Is there anything in the buffer?
    if (buffer != NULL)
    {
        // Append the data to the buffer
        buffer->append(data, size * nmemb);

        // How much did we write?
        result = size * nmemb;
    }

    return result;
}

int http_post(string data = "")
{
    // Our curl objects
    CURL *curl;
    CURLcode result;

    // Create our curl handle
    curl = curl_easy_init();

    if (curl)
    {
        // Now set up all of the curl options
        // curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "charsets: utf-8");

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, post_url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_HEADER, 1); 
        //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

        //authentification 
        if (!post_auth.empty()) 
        {
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC); 
            curl_easy_setopt(curl, CURLOPT_USERPWD, post_auth.c_str());
        }

        if (!data.empty())
        { 
            curl_easy_setopt(curl, CURLOPT_POST, 1);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        }

        // Attempt to retrieve the remote page
        result = curl_easy_perform(curl);

        // Always cleanup
        curl_easy_cleanup(curl);

        // Did we succeed?
        if (result == CURLE_OK)
        {
            if (opt_verbose)
            { 
                cout << buffer << "\n";
            }
        }
        else
        {
            cout << "Error";
            exit(-1);
        }
    }
    return 0;
}

void parseTextField(const char * line)
{
    string s;
    s.assign(line);
    string keyvalue;
    string keyname;
    boost::regex re("<(\\w+)>([\\w\\/\\- \\+\\.]+)<\\/[\\w]+>");
    const int subs[] = {1, 2,};
    boost::sregex_token_iterator i(s.begin(), s.end(), re, subs);
    boost::sregex_token_iterator j;
    boost::property_tree::ptree props;

    //    boost::property_tree::write_json("prob.json", props);
    unsigned count = 0;
    while(i != j)
    {
        if (count%2 == 0)
            keyname.assign(*i++);
        else
        {
            keyvalue.assign(*i++);
            props.push_back(std::make_pair(keyname, keyvalue));
        }
        count++;
    }

    if (opt_post)
    {
        std::stringstream ss;
        boost::property_tree::write_json(ss, props);       
        http_post(ss.str());
    }
}

void writeLog(const char * message)
{
    std::ofstream myfile;
    myfile.open (log_path.c_str(), std::ofstream::app);
    myfile << time(0) << ": " << message << endl;
    myfile.close();
}

struct ZAPI_packet_receiving sendRequest(struct ZAPI_packet_header packet) 
{
    int sockfd;
    if ((sockfd = socket(AF_INET,SOCK_DGRAM, IPPROTO_UDP)) == -1)
        ::error("socket");

    struct sockaddr_in si_srv;
    memset(&si_srv, 0, sizeof(si_srv));

    si_srv.sin_family = AF_INET;
    si_srv.sin_port = htons(zibase_port);
    
    if (inet_aton(zibase_address.c_str(), &si_srv.sin_addr) == 0)
       ::error("inet_aton");

    packet.hton();

    struct sockaddr_in si_client;
    memset(&si_client, 0, sizeof(si_client));

    si_client.sin_family = AF_INET;
    si_client.sin_port = htons(bind_port);
    si_client.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sockfd, (struct sockaddr*)&si_client, sizeof(si_client)) == -1)
        ::error("bind");
    if (opt_verbose)
        std::cout << "sending " << sizeof(packet) << " bytes\n";
    
    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&si_srv, sizeof(si_srv)) == -1)
        ::error("sendto");

    ZAPI_packet_receiving rcvpacket;
    socklen_t slen=sizeof(si_srv);
    recvfrom(sockfd, (void*)&rcvpacket, sizeof(rcvpacket), 0, (struct sockaddr *)&si_srv, &slen);
    
    rcvpacket.ntoh();

    close(sockfd);
 
    return rcvpacket;
}

void listen()
{
    int sockfd;
    if ((sockfd = socket(AF_INET,SOCK_DGRAM, IPPROTO_UDP)) == -1)
        ::error("socket");

    struct sockaddr_in si_srv;
    memset(&si_srv, 0, sizeof(si_srv));

    si_srv.sin_family = AF_INET;
    si_srv.sin_port = htons(zibase_port);
    if (inet_aton(zibase_address.c_str(), &si_srv.sin_addr) == 0)
        ::error("inet_aton");

    ZAPI_packet_header packet(ZIBASE_CMD_HOST_REGISTERING,
         htonl(inet_addr(bind_address.c_str())), 
         bind_port);

    packet.hton();

    struct sockaddr_in si_client;
    memset(&si_client, 0, sizeof(si_client));

    si_client.sin_family = AF_INET;
    si_client.sin_port = htons(bind_port);
    si_client.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr*)&si_client, sizeof(si_client)) == -1)
        ::error("bind");

    if (opt_verbose)
        std::cout << "sending " << sizeof(packet) << " bytes\n";

    if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&si_srv, sizeof(si_srv)) == -1)
        ::error("sendto");

    while (1)
    {
        ZAPI_packet_receiving rcvpacket;
        socklen_t slen=sizeof(si_srv);
        recvfrom(sockfd, (void*)&rcvpacket, sizeof(rcvpacket), 0, (struct sockaddr *)&si_srv, &slen);
        packet.ntoh();
        parseTextField(rcvpacket.text_field);
        
        if (opt_log)
            writeLog(rcvpacket.text_field); 

        if (opt_verbose)
            printf("text_field: %s\n", rcvpacket.text_field);
    }

    close(sockfd);
}

void registering()
{
    ZAPI_packet_receiving rcvpacket;
    ZAPI_packet_header registerpacket(ZIBASE_CMD_HOST_REGISTERING, 
        htonl(inet_addr(bind_address.c_str())), 
        bind_port);

    rcvpacket = sendRequest(registerpacket);
    
    printf("Name : %s\n", rcvpacket.header);
    printf("Cmd %d\n", rcvpacket.command);
   if (opt_verbose)
     printf("text_field: %s\n", rcvpacket.text_field);
}

void unregistering()
{
    ZAPI_packet_receiving rcvpacket;
    ZAPI_packet_header unregisterpacket(ZIBASE_CMD_HOST_UNREGISTERING, 
        htonl(inet_addr(bind_address.c_str())), 
        bind_port);

    rcvpacket = sendRequest(unregisterpacket);
}

int main(int ac, char* av[])
{
    //http://stackoverflow.com/questions/5700466/c-c-unix-configuration-file-library
    //http://netzmafia.de/skripten/unix/linux-daemon-howto.html
    
            /* Our process ID and Session ID */
        pid_t pid, sid;
        
        /* Fork off the parent process */
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }

        /* Change the file mode mask */
        umask(0);
                
        /* Open any logs here */        
                
        /* Create a new SID for the child process */
        sid = setsid();
        if (sid < 0) {
                /* Log the failure */
                exit(EXIT_FAILURE);
        }
        

        
        /* Change the current working directory */
        if ((chdir("/")) < 0) {
                /* Log the failure */
                exit(EXIT_FAILURE);
        }
        
    // Declare the supported options.
    po::options_description desc("Allowed options");
    desc.add_options()
       ("help", "produce help message")
       ("verbose", "shows details about the results of running zibased")
       ("bind-address", po::value<string>(&bind_address), "")
       ("bind-port", po::value<int>(&bind_port)->default_value(49998), "")
       ("zibase-port",  po::value<int>(&zibase_port)->default_value(49999), "")
       ("zibase-address", po::value<string>(&zibase_address), "")
       ("post-url",  po::value<string>(&post_url), "")
       ("post-auth",  po::value<string>(&post_auth), "")
       ("log-file", po::value<string>(&log_path)->default_value("/tmp/zibase.log"), "")
    ;
    
    po::variables_map vm;
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);
    
    if (vm.count("help")) {
        cout << desc << "\n";
        return 1;
    }

    if (vm.count("verbose")) {
        cout << "verbose:ok\n";
        opt_verbose = true;
    }
    
    if (vm.count("log-file")) {
        cout << "logfile:" << log_path << "\n";
        opt_log = true;
    } 

    if (!vm.count("bind-address")) {
        cout << "Error: you must specify the bind address." << endl;
        return 1;
    } else {
        cout << "bind-address:" << bind_address << "\n";
    }
    
    if (!vm.count("zibase-address")) {
        cout << "Error: you must specify the zibase address." << endl;
        return 1;
    } else {
        cout << "bind-address:" << zibase_address << "\n";
    }

    if (vm.count("verbose")) {
        opt_verbose = true;    
    } 

    if (vm.count("post-url")) {
        cout << "post-url:" << post_url << "\n";
        opt_post = true;
    }    
   
    /* Close out the standard file descriptors */
   //close(STDIN_FILENO);
   //close(STDOUT_FILENO);
   //close(STDERR_FILENO);
    registering();
    listen();
    exit(EXIT_SUCCESS);
}
