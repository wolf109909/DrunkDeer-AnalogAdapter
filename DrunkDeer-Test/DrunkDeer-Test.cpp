#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Xinput.h>
#include <cstdio> // printf
#include <thread>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <ViGEm/Client.h>
#include <limits>
#include <hidapi.h>
#include <atomic>
#include "conmanip.h"
#pragma comment(lib, "setupapi.lib")

#undef max
#undef min


struct hid_device_info* devs;
bool g_should_track_keys = true;
hid_device* g_hid_device_handle = 0;
unsigned char g_key_height_array[128];
XINPUT_STATE g_xinput_state;
DWORD g_xinput_packet_count = 0;
std::atomic_bool should_send = false;

hid_device* open_target_device(struct hid_device_info* cur_dev) {
	for (; cur_dev; cur_dev = cur_dev->next) {
		if (cur_dev->vendor_id == 0x352D && cur_dev->product_id == 0x2383 && cur_dev->usage == 0x0)
		{
			printf("Device found: %s\n", cur_dev->path);
			return hid_open_path(cur_dev->path);
		}
	}
}

template<typename Num>
Num scale(double p) {
	return (Num)(p * std::numeric_limits<Num>::max() + std::numeric_limits<Num>::min() - p * std::numeric_limits<Num>::min());
}



// packet functions
int sendpkt_request_keys()
{
	const unsigned char buf[4] = { 0x04, 0xb6, 0x03, 0x01 };
	return hid_write(g_hid_device_handle, buf, 4);
}

// receive process
void receive_packet_controller(PVIGEM_CLIENT client, PVIGEM_TARGET pad, unsigned char* buffer)
{
	// not our device
	if (buffer[0] != 0x04) {
		return;
	}

	unsigned char command = buffer[1];
	unsigned char byte2 = buffer[2];
	unsigned char byte3 = buffer[3];
	unsigned char byte4 = buffer[4];
	unsigned char byte5 = buffer[5];
	unsigned char byte6 = buffer[6];
	unsigned char byte7 = buffer[7];

	SHORT thumb_rx = 0;

	if (command != 0xb7)
	{
		// we only need key height message here
		return;
	}

	int base_position = 0;
	int operation_length = 0;
	if (byte4 == 0)
	{
		base_position = 0;
		operation_length = 59;
	}
	else if (byte4 == 1)
	{
		base_position = 59;
		operation_length = 59;
	}
	else
	{
		base_position = 118;
		operation_length = 8;
		should_send.store(true);
	}
	for (int i = 0; i < operation_length; i++)
	{
		int keynum = base_position + i;
		unsigned char new_value = buffer[i + 4];
		double p = new_value / 40.0;
		//std::cout << "key: " << base_position + i << " , height: " << int(new_value) << std::endl;
		if (new_value < 2)
		{
			new_value = 0;
		}
		switch (keynum)
		{
		case 99:
		{
			g_xinput_state.Gamepad.bRightTrigger = scale<BYTE>(p);
			//printf("RT: %d\n", g_xinput_state.Gamepad.bRightTrigger);
			break;
		}
		case 121:
		{
			g_xinput_state.Gamepad.bLeftTrigger = scale<BYTE>(p);
			//printf("LT: %d\n", g_xinput_state.Gamepad.bLeftTrigger);
			break;
		}
		case 120:
		{
			//LEFT
			thumb_rx += -scale<SHORT>(p * 0.5 + 0.5);
			break;
		}
		case 122:
		{
			//RIGHT
			thumb_rx += scale<SHORT>(p * 0.5 + 0.5);
			break;
		}
		default:
			break;
		}
	}
	// UP->99
	// DOWN->121
	// LEFT->120
	// RIGHT->122
	if (byte4 != 0x00 && byte4 != 0x01) {
		g_xinput_state.Gamepad.sThumbLX = thumb_rx;

		//printf("TRX: \b\b\b\bLX:\n");


		vigem_target_x360_update(client, pad, *reinterpret_cast<XUSB_REPORT*>(&g_xinput_state.Gamepad));
	}
}


void receive_packet_handler(char* buffer) {


	// not our device
	if (buffer[0] != 0x04) {
		return;
	}

	unsigned char command = buffer[1];
	unsigned char byte2 = buffer[2];
	unsigned char byte3 = buffer[3];
	unsigned char byte4 = buffer[4];
	unsigned char byte5 = buffer[5];
	unsigned char byte6 = buffer[6];
	unsigned char byte7 = buffer[7];

	switch (command) {
	case 0xa0: {
		if (byte2 == 0x02) {
			if (byte3 == 0) {
				// keyboard respond identify command
				printf("keyboard:%d %d %d\n", byte5, byte6, byte7);
				return;
			}
		}
		break;
	}

	case 0xb7:
	{
		int base_position = 0;
		int operation_length = 0;
		if (byte4 == 0)
		{
			base_position = 0;
			operation_length = 59;
		}
		else if (byte4 == 1)
		{
			base_position = 59;
			operation_length = 59;
		}
		else
		{
			base_position = 118;
			operation_length = 8;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			sendpkt_request_keys();

		}
		for (int i = 0; i < operation_length; i++)
		{
			unsigned char new_value = buffer[i + 4];
			//std::cout << "key: " << base_position + i << " , height: " << int(new_value) << std::endl;
			if (new_value < 2)
			{
				new_value = 0;
			}
			if (new_value != g_key_height_array[base_position + i])
			{
				g_key_height_array[base_position + i] = new_value;
				std::cout << "key: " << base_position + i << " , height: " << new_value * 100.0 / 40.0 << std::endl;
			}
		}
	}
	default:
		break;
	}
}

using namespace conmanip;

void ShowConsoleCursor(bool showFlag)
{
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	CONSOLE_CURSOR_INFO     cursorInfo;

	GetConsoleCursorInfo(out, &cursorInfo);
	cursorInfo.bVisible = showFlag; // set the cursor visibility
	SetConsoleCursorInfo(out, &cursorInfo);
}


int main(int argc, char* argv[])
{
	ShowConsoleCursor(false);
	console_out_context ctxout;
	console_out conout(ctxout);
	// ===============================================
	// keyboard setup procedures
	devs = hid_enumerate(0x0, 0x0);
	//print_devices_with_descriptor(devs);
	int res;
	// Initialize the hidapi library
	res = hid_init();
	// Open the device
	g_hid_device_handle = open_target_device(devs);
	hid_free_enumeration(devs);
	if (!g_hid_device_handle) {
		printf("Unable to open device\n");
		hid_exit();
		return 1;
	}
	hid_set_nonblocking(g_hid_device_handle, 1);
	// end of keyboard setup procedures
	// ===============================================


	// ===============================================
	// vigem setup procedures
	std::cout << "Allocating a new virtual xbox360 controller !" << std::endl;
	const auto client = vigem_alloc();

	if (client == nullptr)
	{
		std::cerr << "Uh, not enough memory to do that?!" << std::endl;
		return -1;
	}
	const auto retval = vigem_connect(client);

	if (!VIGEM_SUCCESS(retval))
	{
		std::cerr << "ViGEm Bus connection failed with error code: 0x" << std::hex << retval << std::endl;
		return -1;
	}
	// new handle for virtual pad
	const auto pad = vigem_target_x360_alloc();
	// add the pad
	const auto pir = vigem_target_add(client, pad);

	if (!VIGEM_SUCCESS(pir))
	{
		std::cerr << "Target plugin failed with error code: 0x" << std::hex << pir << std::endl;
		return -1;
	}
	// end of vigem setup procedures
	// ===============================================


	// initial packet used to trigger the loop
	sendpkt_request_keys();

	// dedicated thread for sending delayed packet request to the keyboard
	std::thread send_thread([]() {
		while (true) {
			if (should_send.load()) {
				sendpkt_request_keys();
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		});
	send_thread.detach();


	// read buffer
	unsigned char hid_readbuffer[65];
	while (true)
	{
		// main loop for reading device response
		res = hid_read(g_hid_device_handle, hid_readbuffer, sizeof(hid_readbuffer));
		if (res < 0) {
			printf("Unable to read(): %ls\n", hid_error(g_hid_device_handle));
			break;
		}
		else if (res > 0)
		{
			receive_packet_controller(client, pad, hid_readbuffer);
			conout.setpos(0, 10);
			std::cout << "ThumbRightX:  " << g_xinput_state.Gamepad.sThumbLX << "        ";
			conout.setpos(0, 11);
			std::cout << "LeftTrigger:  " << int(g_xinput_state.Gamepad.bLeftTrigger) << "        ";
			conout.setpos(0, 12);
			std::cout << "RightTrigger: " << int(g_xinput_state.Gamepad.bRightTrigger) << "        ";
		}
	}

	printf("Exiting...\n");
	hid_close(g_hid_device_handle);

	res = hid_exit();
	system("pause");
	return 0;
}