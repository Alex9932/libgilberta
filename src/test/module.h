#ifndef MODULE_H
#define MODULE_H

#define TESTMODULE_ECHO 0 // Example echo server/client
#define TESTMODULE_MSG  1 // Simple console messanger

#define GLB_GENERATE_CRC16_TABLE 0 // Generate CRC16 table for testing purposes

#define GLB_TEST_CLIENT 0 // Force client mode for testing (connect to 127.0.0.1:12345)


#ifdef __cplusplus
extern "C" {
#endif

int ModuleMain(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif