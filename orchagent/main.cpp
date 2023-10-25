extern "C" {
#include "otai.h"
#include "otaistatus.h"
}

#include <fstream>
#include <iostream>
#include <unordered_map>
#include <map>
#include <memory>
#include <thread>
#include <chrono>
#include <getopt.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "timestamp.h"

#include <otairedis.h>
#include <logger.h>

#include "orchdaemon.h"
#include "otai_serialize.h"
#include "otaihelper.h"
#include <signal.h>

using namespace std;
using namespace swss;

extern otai_linecard_api_t *otai_linecard_api;

/* Global variables */
otai_object_id_t gLinecardId = OTAI_NULL_OBJECT_ID;

#define DEFAULT_BATCH_SIZE  128
int gBatchSize = DEFAULT_BATCH_SIZE;
int gSlotId = 0;
bool gLairedisRecord = true;
bool gSwssRecord = true;
bool gLogRotate = false;
bool gLaiRedisLogRotate = false;
bool gSyncMode = false;
otai_redis_communication_mode_t gRedisCommunicationMode = OTAI_REDIS_COMMUNICATION_MODE_REDIS_ASYNC;
string otairedis_rec_filename;
string gFlexcounterJsonFile;
ofstream gRecordOfs;
string gRecordFile;

void usage()
{
    cout << "usage: orchagent [-h] [-r record_type] [-d record_location] [-f swss_rec_filename] [-j otairedis_rec_filename] [-b batch_size] [-m MAC] [-i INST_ID] [-s] [-z mode]" << endl;
    cout << "    -h: display this message" << endl;
    cout << "    -r record_type: record orchagent logs with type (default 3)" << endl;
    cout << "                    0: do not record logs" << endl;
    cout << "                    1: record OTAI call sequence as otairedis.rec" << endl;
    cout << "                    2: record SwSS task sequence as swss.rec" << endl;
    cout << "                    3: enable both above two records" << endl;
    cout << "    -d record_location: set record logs folder location (default .)" << endl;
    cout << "    -b batch_size: set consumer table pop operation batch size (default 128)" << endl;
    cout << "    -i INST_ID: set the ASIC instance_id in multi-asic platform" << endl;
    cout << "    -s: enable synchronous mode (deprecated, use -z)" << endl;
    cout << "    -z: redis communication mode (redis_async|redis_sync|zmq_sync), default: redis_async" << endl;
    cout << "    -f swss_rec_filename: swss record log filename(default 'swss.rec')" << endl;
    cout << "    -j otairedis_rec_filename: otairedis record log filename(default otairedis.rec)" << endl;
    cout << "    -c flexcounter_json_filename: flexcounter json filename" << endl;
}

void sighup_handler(int signo)
{
    /*
     * Don't do any logging since they are using mutexes.
     */
    gLogRotate = true;
    gLaiRedisLogRotate = true;
}

int main(int argc, char **argv)
{
    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_DEBUG);

    SWSS_LOG_ENTER();

    swss::Logger::getInstance().setMinPrio(swss::Logger::SWSS_NOTICE);

    swss::Logger::linkToDbNative("orchagent");

    if (signal(SIGHUP, sighup_handler) == SIG_ERR)
    {
        SWSS_LOG_ERROR("failed to setup SIGHUP action");
        exit(1);
    }

    int opt;

    string record_location = ".";
    string swss_rec_filename = "swss.rec";

    while ((opt = getopt(argc, argv, "b:m:r:f:j:d:i:hsz:c:")) != -1)
    {
        switch (opt)
        {
        case 'b':
            gBatchSize = atoi(optarg);
            break;
        case 'i':
            gSlotId = atoi(optarg) + 1;
            break;
        case 'r':
            if (!strcmp(optarg, "0"))
            {
                gLairedisRecord = false;
                gSwssRecord = false;
            }
            else if (!strcmp(optarg, "1"))
            {
                gSwssRecord = false;
            }
            else if (!strcmp(optarg, "2"))
            {
                gLairedisRecord = false;
            }
            else if (!strcmp(optarg, "3"))
            {
                continue; /* default behavior */
            }
            else
            {
                usage();
                exit(EXIT_FAILURE);
            }
            break;
        case 'd':
            record_location = optarg;
            if (access(record_location.c_str(), W_OK))
            {
                SWSS_LOG_ERROR("Failed to access writable directory %s", record_location.c_str());
                exit(EXIT_FAILURE);
            }
            break;
        case 'h':
            usage();
            exit(EXIT_SUCCESS);
        case 's':
            gSyncMode = true;
            SWSS_LOG_NOTICE("Enabling synchronous mode");
            break;
        case 'z':
            otai_deserialize_redis_communication_mode(optarg, gRedisCommunicationMode);
            break;
        case 'f':
            if (optarg)
            {
                swss_rec_filename = optarg;
            }
            break;
        case 'j':
            if (optarg)
            {
                otairedis_rec_filename = optarg;
            }
            break;
        case 'c':
            if (optarg)
            {
                gFlexcounterJsonFile = optarg;
            }
            break;
        default: /* '?' */
            exit(EXIT_FAILURE);
        }
    }

    SWSS_LOG_NOTICE("--- Starting Orchestration Agent ---");

    initLaiApi();
    if (gSwssRecord)
    {
        gRecordFile = record_location + "/" + swss_rec_filename; 
        gRecordOfs.open(gRecordFile, std::ofstream::out | std::ofstream::app);
        if (!gRecordOfs.is_open())
        {
            SWSS_LOG_ERROR("Failed to open SwSS recording file %s", gRecordFile.c_str());
            exit(EXIT_FAILURE);
        }
        gRecordOfs << getTimestamp() << "|recording started" << endl;
    }


    /* Initialize orchestration components */
    DBConnector appl_db("APPL_DB", 0);
    DBConnector config_db("CONFIG_DB", 0);
    DBConnector state_db("STATE_DB", 0);

    auto orchDaemon = make_shared<OrchDaemon>(&appl_db, &config_db, &state_db);

    if (!orchDaemon->init())
    {
        SWSS_LOG_ERROR("Failed to initialize orchestration daemon");
        exit(EXIT_FAILURE);
    }

    orchDaemon->start();

    return 0;
}

