/*
 * Copyright 2017 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

extern "C" {

#include "common.h"
}
bool g_ci_mode = false;
bool g_monitor_mode = false;
bool g_server_mode = false;

#include "rt_analyzer_subscriber.h"
#include "log_manager_subscriber.h"
#include "monitor_test.h"
#include "logging_parser.h"

#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>

using namespace std;

namespace log_mgr_tests {

    class LogMgrTestEnv : public ::testing::Environment {
        public:
            virtual void SetUp() {
                ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
                ssd_conf << "FILE_NAME ./data/ssd.img\n"
                    "PAGE_SIZE 1048576\n" // bytes in Mb
                    "PAGE_NB 2\n"
                    "SECTOR_SIZE 1048576\n" // bytes in Mb
                    "FLASH_NB 4\n"
                    "BLOCK_NB 8\n"
                    "PLANES_PER_FLASH 1\n"
                    "REG_WRITE_DELAY 82\n"
                    "CELL_PROGRAM_DELAY 900\n"
                    "REG_READ_DELAY 82\n"
                    "CELL_READ_DELAY 50\n"
                    "BLOCK_ERASE_DELAY 2000\n"
                    "CHANNEL_SWITCH_DELAY_R 16\n"
                    "CHANNEL_SWITCH_DELAY_W 33\n"
                    "CHANNEL_NB 4\n"
                    "STAT_TYPE 15\n"
                    "STAT_SCOPE 62\n"
                    "STAT_PATH /tmp/stat.csv\n"
                    "STORAGE_STRATEGY 1\n";
                ssd_conf.close();

                INIT_SSD_CONFIG();

                if (g_server_mode) {
                    log_server_init();
                    pthread_create(&_server, NULL, log_server_run, NULL);
                    printf("Server opened\n");
                    printf("Browse to http://127.0.0.1:%d/ to see the statistics\n",
                            LOG_SERVER_PORT);
                }

                if (g_monitor_mode) {
                    pthread_create(&_monitor, NULL, run_monitor, NULL);
                    printf("Monitor opened\n");
                }
            }

            virtual void TearDown() {

                if (g_monitor_mode) {
                    printf("Waiting for monitor to close...\n");
                    pthread_join(_monitor, NULL);
                    printf("Monitor closed\n");
                }

                if (g_server_mode) {
                    printf("Waiting for server to close...\n");
                    pthread_join(_server, NULL);
                    printf("Server closed\n");
                    log_server_free();
                }

                remove("./data/ssd.conf");
            }
        protected:
            pthread_t _monitor;
            pthread_t _server;
    };
    LogMgrTestEnv* testEnv;

    class LogMgrUnitTest : public ::testing::TestWithParam<size_t> {
        public:
            const static char TEST_STRING[];

            virtual void SetUp() {
                size_t logger_size = GetParam();
                _logger = logger_init(logger_size);
            }
            virtual void TearDown() {
                logger_free(_logger);
            }
        protected:
            Logger_Pool* _logger;
    };
    const char LogMgrUnitTest::TEST_STRING[] = "Test Me Please";


    /**
     * Calculate the different buffer sizes for the logger
     * @return the different buffer sizes for the logger
     */
    std::vector<size_t> GetParams() {
        std::vector<size_t> list;

        // push the different sizes of the buffer, in bytes
        list.push_back(1);
        list.push_back(2);
        list.push_back(3);
        list.push_back(10);

        return list;
    }

    INSTANTIATE_TEST_CASE_P(LoggerSize, LogMgrUnitTest, ::testing::ValuesIn(GetParams()));

    /**
     * Test reading back a written string:
     * - make sure normal writing works
     * - make sure normal reading works
     */
    TEST_P(LogMgrUnitTest, NormalStringWriteRead) {
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test read before write on log boundry
     * Fill the log with data then read it and try to read
     * One more byte
     */
    TEST_P(LogMgrUnitTest, ReadBeforeWriteOnLogBoundry) {
        Byte placeholder = 'x';
        char res[5];

        for (unsigned int i = 0; i < ((LOG_SIZE)-1); i++)
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        for (unsigned int i = 0; i < ((LOG_SIZE)-1); i++)
            ASSERT_EQ(1, logger_read(_logger, (Byte *)res, 1));
        ASSERT_EQ(0, logger_read(_logger, (Byte *)res, 5));
    }
    /**
     * Test filling all the allocated log's with test string and reading it back:
     * - make sure normal writing works
     * - make sure normal reading works
     */
    TEST_P(LogMgrUnitTest, NormalStringFillWriteRead) {
        char res[sizeof(TEST_STRING)];

        for (unsigned int i = 0;
                i < (unsigned int)((LOG_SIZE * _logger->number_of_allocated_logs)/sizeof(TEST_STRING));
                i++){
            ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        }

        for (unsigned int i = 0;
                i < (unsigned int)((LOG_SIZE * _logger->number_of_allocated_logs)/sizeof(TEST_STRING));
                i++)
        {
            ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
            ASSERT_STREQ(res, TEST_STRING);
            memset(res,0, sizeof(TEST_STRING));
        }
    }

    /**
     * Test filling up the log and then writting more data:
     * - make sure filling up the buffer returns zero
     */
    TEST_P(LogMgrUnitTest, FullBuffer) {
        Byte offset[4];
        Byte placeholder = 'y';
        // write and read the offset
        ASSERT_EQ(0, logger_write(_logger, offset, sizeof(offset)));
        ASSERT_EQ(sizeof(offset), logger_read(_logger, offset, sizeof(offset)));
        // almost fill the buffer
        for (unsigned int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++)
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        // try to fill the buffer completely (one slot empty means a full buffer)
        ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
    }
    /**
     * Test writting and reading when all the log's
     * in logger_pool are full:
     * - make sure writing on full logger_pool works
     * - make sure reading on full logger_pool works
     */
    TEST_P(LogMgrUnitTest, FullPoolStringWriteRead) {
        // fill the buffer
        Byte placeholder = 'x';
        for (unsigned long int i = 0; i < ((LOG_SIZE * _logger->number_of_allocated_logs)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // write test string
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        // read full log
        for (unsigned long int i = 0; i < (LOG_SIZE * _logger->number_of_allocated_logs); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // read the written test string
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test reading back a string which was written on the boundary of the buffer:
     * - make sure writing at buffer boundary works
     * - make sure reading from buffer boundary works
     */
    TEST_P(LogMgrUnitTest, WrapStringWriteRead) {
        // fill the buffer
        Byte placeholder = 'x';
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // increment the reader
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // write the string and read it back
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));
        char res[2 * sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test reading back a string which was half written of one log and half on another:
     * - make sure writing and crossing the buffer boundary works
     */
    TEST_P(LogMgrUnitTest, CrossBoundryStringWriteRead) {
        Byte placeholder = 'x';

        // fill the buffer and leave enough place for half of the test string
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)/2); i++) {
            ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        }
        // write the test string
        ASSERT_EQ(0, logger_write(_logger, (Byte*) TEST_STRING, sizeof(TEST_STRING)));

        // increment the reader until the start fo the test string
        for (unsigned int i = 0; i < LOG_SIZE - (sizeof(TEST_STRING)/2); i++) {
            ASSERT_EQ(1, logger_read(_logger, &placeholder, 1));
        }
        // read the string back
        char res[sizeof(TEST_STRING)];
        ASSERT_EQ(sizeof(TEST_STRING), logger_read(_logger, (Byte*) res, sizeof(TEST_STRING)));
        ASSERT_STREQ(res, TEST_STRING);
    }
    /**
     * Test reading the logger after successive writes:
     * - make sure the buffer works as a queue, and not a stack
     *   (read in the same order of the write)
     */
    TEST_P(LogMgrUnitTest, SuccessiveWrites) {
        Byte first = 'X';
        Byte second = 'Y';
        Byte first_res;
        Byte second_res;
        ASSERT_EQ(0, logger_write(_logger, &first, 1));
        ASSERT_EQ(0, logger_write(_logger, &second, 1));
        ASSERT_EQ(1, logger_read(_logger, &first_res, 1));
        ASSERT_EQ(1, logger_read(_logger, &second_res, 1));
        ASSERT_EQ(first, first_res);
        ASSERT_EQ(second, second_res);
    }
    /**
     * Test reading before writing:
     * - make sure reading before any writing doesn't work
     * - make sure reading after writing does work
     */
    TEST_P(LogMgrUnitTest, ReadBeforeWrite) {
        Byte placeholder = 'J';
        Byte res;
        ASSERT_EQ(0, logger_read(_logger, &res, 1));
        ASSERT_EQ(0, logger_write(_logger, &placeholder, 1));
        ASSERT_EQ(1, logger_read(_logger, &res, 1));
        ASSERT_EQ(placeholder, res);
    }

    /* Unit tests for the different logs */
    /**
     * Test writing and reading a physical cell read log
     */
    TEST_P(LogMgrUnitTest, PhysicalCellRead) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        PhysicalCellReadLog log = {
            .channel = 3,
            .block = 80,
            .page = 123,
            .metadata = {start, start+1}
        };
        LOG_PHYSICAL_CELL_READ(_logger, log);
        ASSERT_EQ(PHYSICAL_CELL_READ_LOG_UID, next_log_type(_logger));
        PhysicalCellReadLog res = NEXT_PHYSICAL_CELL_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a physical cell program log
     */
    TEST_P(LogMgrUnitTest, PhysicalCellProgram) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        PhysicalCellProgramLog log = {
            .channel = 15,
            .block = 63,
            .page = 50,
            .metadata = {start, start+1}
        };
        LOG_PHYSICAL_CELL_PROGRAM(_logger, log);
        ASSERT_EQ(PHYSICAL_CELL_PROGRAM_LOG_UID, next_log_type(_logger));
        PhysicalCellProgramLog res = NEXT_PHYSICAL_CELL_PROGRAM_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a logical cell program log
     */
    TEST_P(LogMgrUnitTest, LogicalCellProgram) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        LogicalCellProgramLog log = {
            .channel = 2,
            .block = 260,
            .page = 3,
            .metadata = {start, start+1}
        };
        LOG_LOGICAL_CELL_PROGRAM(_logger, log);
        ASSERT_EQ(LOGICAL_CELL_PROGRAM_LOG_UID, next_log_type(_logger));
        LogicalCellProgramLog res = NEXT_LOGICAL_CELL_PROGRAM_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.block, res.block);
        ASSERT_EQ(log.page, res.page);
    }
    /**
     * Test writing and reading a garbage collection log
     */
    TEST_P(LogMgrUnitTest, GarbageCollection) {
        GarbageCollectionLog log = { };
        LOG_GARBAGE_COLLECTION(_logger, log);
        ASSERT_EQ(GARBAGE_COLLECTION_LOG_UID, next_log_type(_logger));
        // test that NEXT_GARBAGE_COLLECTION_LOG actually does nothing,
        // due to the fact that the struct is empty
        Byte placeholder;
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 0));
        NEXT_GARBAGE_COLLECTION_LOG(_logger);
        ASSERT_EQ(0, logger_read(_logger, &placeholder, 0));
    }
    /**
     * Test writing and reading a register read log
     */
    TEST_P(LogMgrUnitTest, RegisterReadLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        RegisterReadLog log = {
            .channel = 10,
            .die = 15,
            .reg = 37,
            .metadata = {start, start+1}
        };
        LOG_REGISTER_READ(_logger, log);
        ASSERT_EQ(REGISTER_READ_LOG_UID, next_log_type(_logger));
        RegisterReadLog res = NEXT_REGISTER_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.reg, res.reg);
    }
    /**
     * Test writing and reading a register write log
     */
    TEST_P(LogMgrUnitTest, RegisterWriteLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        RegisterWriteLog log = {
            .channel = 87013,
            .die = 225034,
            .reg = 4,
            .metadata = {start, start+1}
        };
        LOG_REGISTER_WRITE(_logger, log);
        ASSERT_EQ(REGISTER_WRITE_LOG_UID, next_log_type(_logger));
        RegisterWriteLog res = NEXT_REGISTER_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.reg, res.reg);
    }
    /**
     * Test writing and reading a block erase log
     */
    TEST_P(LogMgrUnitTest, BlockEraseLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        BlockEraseLog log = {
            .channel = 6,
            .die = 352,
            .block = 947,
            .metadata = {start, start+1}
        };
        LOG_BLOCK_ERASE(_logger, log);
        ASSERT_EQ(BLOCK_ERASE_LOG_UID, next_log_type(_logger));
        BlockEraseLog res = NEXT_BLOCK_ERASE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
        ASSERT_EQ(log.die, res.die);
        ASSERT_EQ(log.block, res.block);
    }
    /**
     * Test writing and reading a channel switch to read log
     */
    TEST_P(LogMgrUnitTest, ChannelSwitchToReadLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        ChannelSwitchToReadLog log = {
            .channel = 73,
            .metadata = {start, start+1}
        };
        LOG_CHANNEL_SWITCH_TO_READ(_logger, log);
        ASSERT_EQ(CHANNEL_SWITCH_TO_READ_LOG_UID, next_log_type(_logger));
        ChannelSwitchToReadLog res = NEXT_CHANNEL_SWITCH_TO_READ_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
    }
    /**
     * Test writing and reading a channel switch to read write
     */
    TEST_P(LogMgrUnitTest, ChannelSwitchToWriteLog) {
        struct timeval logging_parser_tv;
        TIME_MICROSEC(start);
        ChannelSwitchToWriteLog log = {
            .channel = 3,
            .metadata = {start, start+1}
        };
        LOG_CHANNEL_SWITCH_TO_WRITE(_logger, log);
        ASSERT_EQ(CHANNEL_SWITCH_TO_WRITE_LOG_UID, next_log_type(_logger));
        ChannelSwitchToWriteLog res = NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(_logger);
        ASSERT_EQ(log.channel, res.channel);
    }

    /* Real Time Analyzer Tests */
    /**
     * Do a simple test of the real time log analyzer
     */
    TEST_P(LogMgrUnitTest, BasicRTAnalyzer) {
        RTLogAnalyzer* analyzer = rt_log_analyzer_init(_logger);
        rt_subscriber::subscribe(analyzer);
        if (g_monitor_mode)
            rt_log_analyzer_subscribe(analyzer, update_stats, NULL);
        if (g_server_mode)
            rt_log_analyzer_subscribe(analyzer, log_server_update, NULL);
        rt_subscriber::write();
        rt_subscriber::read();
        rt_log_analyzer_free(analyzer, 0);
    }

    /* Log Manager Tests */
    /**
     * Do a simple test of the log manager
     */
    TEST_P(LogMgrUnitTest, BasicLogManager) {
        LogManager* manager = log_manager_init();
        manager_subscriber::init(manager);
        if (g_monitor_mode)
            log_manager_subscribe(manager, update_stats, NULL);
        if (g_server_mode)
            log_manager_subscribe(manager, log_server_update, NULL);
        manager_subscriber::run();
        manager_subscriber::free();
        log_manager_free(manager);
    }
} //namespace

/**
 * Run the log manager tests
 * @param argc the number of parameters provided in `argv`
 * @param argv the parameters which can be provided to the script. Includes:
 *        - `--ci`: run the tests in continous integration mode. Currently changes nothing.
 *        - `--show-monitor`: display the QT monitor while running
 *        - `--run-server`: open the web server while running
 */
int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ci") == 0) {
            g_ci_mode = true;
        }
        else if (strcmp(argv[i], "--show-monitor") == 0) {
            g_monitor_mode = true;
        }
        else if (strcmp(argv[i], "--run-server") == 0) {
            g_server_mode = true;
        }
    }
    testing::InitGoogleTest(&argc, argv);
    log_mgr_tests::testEnv = (log_mgr_tests::LogMgrTestEnv*) testing::AddGlobalTestEnvironment(new log_mgr_tests::LogMgrTestEnv);
    return RUN_ALL_TESTS();
}
