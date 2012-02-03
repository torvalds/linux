/* wlmSampleTests.cpp : Sample test program which uses the 
 * 		Wireless LAN Manufacturing (WLM) Test Library.
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlmSampleTests.c,v 1.6 2009/06/10 05:37:13 Exp $
 *
 * The sample tests allow users/customers to understand how WLM
 * Test Library is used to create manufacturing tests.
 * Users/customers may copy and modify the sample tests	as
 * required to meet their specific manufacturing test requirements.
 *
 * The sample tests can run in the following configurations:
 *	direct - on same device as the DUT (default).
 *  socket - as a client to the server DUT over Ethernet.
 *  serial - as a client to the server DUT over serial.
 *  wifi   - as a client to the server DUT over 802.11.
 *  dongle - as a client to the server DUT over serial (to dongle UART).
 * See printUsage() (i.e. --help) for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wlm.h"

/* --------------------------------------------------------------- */
typedef struct
{
	int count;		/* number of tests run */
	int passed;		/* number of tests passed */
	int failed;		/* number of tests failed */
} TestLog;

static TestLog testLog;

#define TEST_INITIALIZE()						\
{												\
	memset(&testLog, 0, sizeof(testLog));		\
}

#define TEST(condition, error)					\
	testLog.count++;							\
	if ((condition)) {							\
		testLog.passed++;						\
	}											\
	else {										\
		testLog.failed++;						\
		printf("\n*** FAIL *** - %s():%d - %s\n\n",	\
			__FUNCTION__, __LINE__, error);		\
	}

#define TEST_FATAL(condition, error)			\
	testLog.count++;							\
	if ((condition)) {							\
		testLog.passed++;						\
	}											\
	else {										\
		testLog.failed++;						\
		printf("\n*** FAIL *** - %s():%d - %s\n\n",	\
			__FUNCTION__, __LINE__, error);		\
		exit(-1);								\
	}

#define TEST_FINALIZE()							\
{												\
	int percent = testLog.count ?				\
		testLog.passed * 100 / testLog.count : 0; \
	printf("\n\n");								\
	printf("Test Summary:\n\n");				\
	printf("Tests    %d\n", testLog.count);		\
	printf("Pass     %d\n", testLog.passed);	\
	printf("Fail     %d\n\n", testLog.failed);	\
	printf("%d%%\n\n", percent);				\
	printf("-----------------------------------\n\n");	\
}

/* --------------------------------------------------------------- */

static void delay(unsigned int msec)
{
	clock_t start_tick = clock();
	clock_t end_tick = start_tick + msec * CLOCKS_PER_SEC / 1000;

	while (clock() < end_tick) {
		/* do nothing */
	}
}

static void testDutInit()
{
	TEST(wlmMinPowerConsumption(TRUE), "unable disable sleep mode");
	TEST(wlmCountryCodeSet(WLM_COUNTRY_ALL), "unable to set country code");
	TEST(wlmGlacialTimerSet(99999), "unable to set glacial timer");
	TEST(wlmFastTimerSet(99999), "unable to set fast timer");
	TEST(wlmSlowTimerSet(99999), "unable to set slow timer");
	TEST(wlmScanSuppress(1), "unable to set scansuppress");
}

static void testVersion()
{
	char buffer[1024];
	TEST(wlmVersionGet(buffer, 1024), "wlmVersionGet failed");
	printf("version info: %s\n", buffer);
}

/* Set transmit power and transmit packets expecting ack response.
 * Reduce transmit power and repeat.
 */
static void testTransmit()
{
	int i;
	int txPower = 17000;	/* power in milli-dB */

	for (i = 0; i < 8; i++) {
		int power;
		unsigned int txPacketCount = 50;
		unsigned int beforeAckCount, afterAckCount, ackCount;

		TEST(wlmTxPowerSet(txPower), "wlmTxPowerSet failed");
		TEST(wlmTxPowerGet(&power), "wlmTxPowerGet failed");
		TEST(power == txPower, "Tx power not set correctly");

		TEST(wlmTxGetAckedPackets(&beforeAckCount), "wlmTxGetAckedPackets failed");

		TEST(wlmTxPacketStart(100, txPacketCount, 1024, "00:11:22:33:44:55", TRUE, TRUE),
			"wlmTxPacketStart failed");

		afterAckCount = beforeAckCount;
		TEST(wlmTxGetAckedPackets(&afterAckCount), "wlmTxGetAckedPackets failed");

		/* another pkteng is required to send ACKs else no ACKs will be received
		 *  i.e.   'wl pkteng_start 00:11:22:33;44:55 rxwithack'
		 */
		ackCount = afterAckCount - beforeAckCount;
		printf("Packets tx=%d, ack=%d\n", txPacketCount, ackCount);
		TEST(ackCount > 0, "no ACK packets received");

		/* reduce tx power */
		txPower -= 1000;
	}

	TEST(wlmTxPacketStop(), "wlmTxPacketStop failed");

	/* set tx power back to default */
	TEST(wlmTxPowerSet(-1), "wlmTxPowerSet failed");
}

/* Batch the commands to set transmit power and transmit packets,
 * reduce transmit power and repeat.
 *
 * During the batching sequence, only 'set' functions can be invoked.
 * 'get' functions are not supported by command batching.
 */
static void testBatchingTransmit(int clientBatching)
{
	int i, count = 8;
	int txPower = 17000;	/* power in milli-dB */
	unsigned int txPacketCount = 50;
	unsigned int beforeAckCount, afterAckCount, ackCount;

	TEST(wlmTxGetAckedPackets(&beforeAckCount), "wlmTxGetAckedPackets failed");

	/* start command batching */
	TEST(wlmSequenceStart(clientBatching), "wlmSequenceStart failed");

		for (i = 0; i < count; i++) {

		TEST(wlmTxPowerSet(txPower), "wlmTxPowerSet failed");

		TEST(wlmTxPacketStart(100, txPacketCount, 1024, "00:11:22:33:44:55", TRUE, TRUE),
			"wlmTxPacketStart failed");

		/* reduce tx power */
		txPower -= 1000;
	}

	TEST(wlmTxPacketStop(), "wlmTxPacketStop failed");

	/* stop command batching */
	TEST(wlmSequenceStop(), "wlmSequenceStop failed");

	afterAckCount = beforeAckCount;
	TEST(wlmTxGetAckedPackets(&afterAckCount), "wlmTxGetAckedPackets failed");

	/* requires pkteng to send ACKs else no ACKs will be received
	 *  i.e. 'wl pkteng_start 00:11:22:33;44:55 rxwithack'
	 */
	ackCount = afterAckCount - beforeAckCount;
	printf("Packets tx=%d, ack=%d\n", txPacketCount * count, ackCount);
	TEST(ackCount > 0, "no ACK packets received");

	/* set tx power back to default */
	TEST(wlmTxPowerSet(-1), "wlmTxPowerSet failed");
}

/* Batch transmit commands on the client */
static void testClientBatchingTransmit()
{
	testBatchingTransmit(TRUE);
}

/* Batch transmit commands on the server */
static void testServerBatchingTransmit()
{
	/* batch commands on server */
	testBatchingTransmit(FALSE);
}

/* Test receiving of packets by waiting for an expected number
 * of packets or a timeout.
 */
static void testReceive()
{
	unsigned int beforeRxPacketCount, afterRxPacketCount, rxPacketCount;
	unsigned int rxPacketExpected = 50;
	unsigned int maxTimeout = 500; 	/* milliseconds */
	int rssi = 0;

	TEST(wlmRxGetReceivedPackets(&beforeRxPacketCount), "wlmRxGetReceivedPackets failed");
	TEST(wlmRxPacketStart("00:11:22:33:44:55", FALSE, TRUE, rxPacketExpected, maxTimeout),
		"wlmRxPacketStart failed");

	afterRxPacketCount = beforeRxPacketCount;
	TEST(wlmRxGetReceivedPackets(&afterRxPacketCount), "wlmRxGetReceivedPackets failed");

	/* requires another pkteng to send packets else no packets will be received
	 *  i.e. 'wl pkteng_start 00:11:22:33:44:55 tx 100 1024 0'
	 */
	rxPacketCount = afterRxPacketCount - beforeRxPacketCount;
	printf("Packets rx=%d\n", rxPacketCount);
	TEST(rxPacketCount > 0, "no packets received");

	TEST(wlmRssiGet(&rssi), "wlmRssiGet failed");
	printf("RSSI=%d\n", rssi);
	TEST(rssi != 0, "no RSSI");

	TEST(wlmRxPacketStop(), "wlmRxPacketStop failed");
}

/* Test joining a network, get and compare the SSID */
static int testJoinNetwork(char *ssid, WLM_AUTH_MODE authMode,
	WLM_ENCRYPTION encryption, const char *key)
{
	char bssidBuf[256];
	int isAssociated;
	char ssidBuf[256];

	/* make sure not currently associated */
	TEST(wlmDisassociateNetwork(), "wlmDisassociateNetwork failed");

	TEST(wlmSecuritySet(WLM_TYPE_OPEN, authMode, encryption, key),
		"wlmSecuritySet failed");
	TEST(wlmJoinNetwork(ssid, WLM_MODE_BSS), "wlmJoinNetwork failed");
	/* delay to allow network association */
	delay(5000);
	TEST(wlmBssidGet(bssidBuf, 256), "wlmBssidGet failed");
	isAssociated = strlen(bssidBuf) == 0 ? FALSE : TRUE;
	if (isAssociated) {
		TEST(wlmSsidGet(ssidBuf, 256), "wlmSsidGet failed");
		printf("associated to SSID=%s BSSID=%s\n", ssidBuf, bssidBuf);
		TEST(strcmp(ssid, ssidBuf) == 0, "SSID does not match");
	}
	else {
		printf("failed to associate to SSID=%s using key=%s\n", ssid, key ? key : "");
	}

	TEST(wlmDisassociateNetwork(), "wlmDisassociateNetwork failed");

	return isAssociated;
}

/* Test joining a network */
static void testJoinNetworkNone()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_NONE", WLM_WPA_AUTH_DISABLED, WLM_ENCRYPT_NONE, 0),
		"testJoinNetworkNone failed");
}

/* Test joining a WEP network */
static void testJoinNetworkWep()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_WEP", WLM_WPA_AUTH_DISABLED, WLM_ENCRYPT_WEP,
		"2222222222444444444466666666668888888888"),
		"testJoinNetworkWep failed");
}

/* Test joining a WPA TKIP network */
static void testJoinNetworkWpaTkip()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_WPA_TKIP", WLM_WPA_AUTH_PSK, WLM_ENCRYPT_TKIP,
		"helloworld"), "testJoinNetworkWpaTkip failed");
}

/* Test joining a WPA AES network */
static void testJoinNetworkWpaAes()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_WPA_AES", WLM_WPA_AUTH_PSK, WLM_ENCRYPT_AES,
		"helloworld"), "testJoinNetworkWpaAes failed");
}

/* Test joining a WPA2 TKIP network */
static void testJoinNetworkWpa2Tkip()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_WPA2_TKIP", WLM_WPA2_AUTH_PSK, WLM_ENCRYPT_TKIP,
		"helloworld"), "testJoinNetworkWpa2Tkip failed");
}

/* Test joining a WPA2 AES network */
static void testJoinNetworkWpa2Aes()
{
	/* requires an AP with the SSID, authentication, and encryption 
	 * configured to match these settings
	 */
	TEST(testJoinNetwork("WLM_WPA2_AES", WLM_WPA2_AUTH_PSK, WLM_ENCRYPT_AES,
		"helloworld"), "testJoinNetworkWpa2Aes failed");
}

static void printUsage(void)
{
	printf("\nUsage: wlmSampleTests [--socket <IP address> [server port] | "
		"--serial <serial port> | --wifi <MAC address> | "
		"--dongle <serial port>] [--linux | --linuxdut]]\n\n");
	printf("--socket - Ethernet between client and server (running 'wl_server_socket')\n");
	printf("      IP address - IP address of server (e.g. 10.200.30.10)\n");
	printf("      server port - Server port number (default 8000)\n\n");
	printf("--serial - Serial between client and server (running 'wl_server_serial')\n");
	printf("      serial port - Client serial port (e.g. 1 for COM1)\n\n");
	printf("--wifi - 802.11 between client and server (running 'wl_server_wifi')\n");
	printf("         (external dongle only, NIC not supported)\n");
	printf("      MAC address - MAC address of wifi interface on dongle "
		"(e.g. 00:11:22:33:44:55)\n\n");
	printf("--dongle - Serial between client and dongle UART (running 'wl_server_dongle')\n");
	printf("      serial port - Client serial port (e.g. COM1 or /dev/ttyS0)\n\n");
	printf("--linux|linuxdut - Server DUT running Linux\n");
}

int main(int argc, char **argv)
{
	WLM_DUT_INTERFACE dutInterface = WLM_DUT_LOCAL;
	char *interfaceName = 0;
	WLM_DUT_SERVER_PORT dutServerPort = WLM_DEFAULT_DUT_SERVER_PORT;
	WLM_DUT_OS dutOs = WLM_DUT_OS_WIN32;

	(void)argc;

	while (*++argv) {

		if (strncmp(*argv, "--help", strlen(*argv)) == 0) {
			printUsage();
			exit(0);
		}

		if (strncmp(*argv, "--socket", strlen(*argv)) == 0) {
			int port;

			dutInterface = WLM_DUT_SOCKET;
			if (!*++argv) {
				printf("IP address required\n");
				printUsage();
				exit(-1);
			}
			interfaceName = *argv;
			if (*++argv && (sscanf(*argv, "%d", &port) == 1)) {
				dutServerPort = (WLM_DUT_SERVER_PORT)port;
			}
			else {
				/* optional parameter */
				--argv;
			}
		}

		if (strncmp(*argv, "--serial", strlen(*argv)) == 0) {
			dutInterface = WLM_DUT_SERIAL;
			if (!*++argv) {
				printf("serial port required\n");
				printUsage();
				exit(-1);
			}
			interfaceName = *argv;
		}

		if (strncmp(*argv, "--wifi", strlen(*argv)) == 0) {
			dutInterface = WLM_DUT_WIFI;
			if (!*++argv) {
				printf("MAC address required\n");
				printUsage();
				exit(-1);
			}
			interfaceName = *argv;
		}

		if (strncmp(*argv, "--dongle", strlen(*argv)) == 0) {
			unsigned int i;
			char buffer[256];

			dutInterface = WLM_DUT_DONGLE;
			if (!*++argv) {
				printf("COM port required\n");
				printUsage();
				exit(-1);
			}
			if (!(sscanf(*argv, "COM%u", &i) == 1 ||
				sscanf(*argv, "/dev/tty%s", buffer) == 1)) {
				printf("serial port invalid\n");
				printUsage();
				exit(-1);
			}
			interfaceName = *argv;
		}

		if ((strncmp(*argv, "--linux", strlen(*argv)) == 0) ||
			strncmp(*argv, "--linuxdut", strlen(*argv)) == 0) {
			dutOs = WLM_DUT_OS_LINUX;
		}
	}

	TEST_INITIALIZE();

	TEST_FATAL(wlmApiInit(), "wlmApiInit failed");

	TEST_FATAL(wlmSelectInterface(dutInterface, interfaceName,
		dutServerPort, dutOs), "wlmSelectInterface failed");

	printf("\n");
	switch (dutInterface) {
		case WLM_DUT_LOCAL:
			printf("Test running against local DUT.\n");
			break;
		case WLM_DUT_SOCKET:
			printf("Test running over Ethernet to remote DUT IP=%s.\n", interfaceName);
			break;
		case WLM_DUT_SERIAL:
			printf("Test running over serial from port %s\n", interfaceName);
			break;
		case WLM_DUT_WIFI:
			printf("Test running over 802.11 to remote DUT MAC=%s.\n", interfaceName);
			break;
		case WLM_DUT_DONGLE:
			printf("Test running over serial from %s to remote dongle UART\n",
				interfaceName);
			break;
		default:
			printf("Invalid interface\n");
			exit(-1);
			break;
	}
	switch (dutOs) {
		case WLM_DUT_OS_LINUX:
			printf("Test running against Linux DUT.\n");
			break;
		case WLM_DUT_OS_WIN32:
			printf("Test running against Win32 DUT.\n");
			break;
		default:
			printf("Invalid DUT OS\n");
			exit(-1);
			break;
	}
	printf("\n");

	/* packet engine requires MPC to be disabled and WLAN interface up */
	TEST(wlmMinPowerConsumption(FALSE), "wlmMinPowerConsuption failed");
	TEST(wlmEnableAdapterUp(TRUE), "wlmEnableAdapterUp failed");
	/* invoke test cases */
	testDutInit();
	testVersion();
	testTransmit();
	testClientBatchingTransmit();
	testServerBatchingTransmit();
	testReceive();
	testJoinNetworkNone();
	testJoinNetworkWep();
	testJoinNetworkWpaTkip();
	testJoinNetworkWpaAes();
	testJoinNetworkWpa2Tkip();
	testJoinNetworkWpa2Aes();

	TEST_FATAL(wlmApiCleanup(), "wlmApiCleanup failed");

	TEST_FINALIZE();
	return 0;
}
