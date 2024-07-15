#define WIN32_LEAN_AND_MEAN
#include "conmanip.h"
#include <ViGEm/Client.h>
#include <Xinput.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <hidapi.h>
#include <iostream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

#pragma comment(lib, "setupapi.lib")

#undef max
#undef min

struct hid_device_info *devs;
bool g_should_track_keys = true;
hid_device *g_hid_device_handle = 0;
unsigned char g_key_height_array[128];
XINPUT_STATE g_xinput_state;
DWORD g_xinput_packet_count = 0;
std::atomic_bool should_send = false;
int g_current_keyboard_identifier = 0;
int g_deadzone_max = 36;
int g_deadzone_min = 2;
int g_polling_interval = 5;
// keyboard packet -> find in keycode2action map -> call action func

typedef void (*pad_action_func)(double percent, bool inverse);

std::map<std::string, pad_action_func> pad_name_lookup_table;

template <typename Num> Num scale(double p) {
  return (Num)(p * std::numeric_limits<Num>::max() +
               std::numeric_limits<Num>::min() -
               p * std::numeric_limits<Num>::min());
}
struct pad_action {
  pad_action_func f;
  bool inverse;
};

std::map<uint8_t, pad_action> g_key_action_map;

std::vector<std::string> keyboard_layout_a75 = {
    "ESC",       "",       "",        "F1",     "F2",     "F3",      "F4",
    "F5",        "F6",     "F7",      "F8",     "F9",     "F10",     "F11",
    "F12",       "KP7",    "KP8",     "KP9",    "u1",     "u2",      "u3",
    "u4",        "SWUNG",  "1",       "2",      "3",      "4",       "5",
    "6",         "7",      "8",       "9",      "0",      "MINUS",   "PLUS",
    "BACK",      "KP4",    "KP5",     "KP6",    "u5",     "u6",      "u7",
    "u8",        "TAB",    "Q",       "W",      "E",      "R",       "T",
    "Y",         "U",      "I",       "O",      "P",      "BRKTS_L", "BRKTS_R",
    "SLASH_K29", "KP1",    "KP2",     "KP3",    "u9",     "u10",     "u11",
    "u12",       "CAPS",   "A",       "S",      "D",      "F",       "G",
    "H",         "J",      "K",       "L",      "COLON",  "QOTATN",  "u13",
    "RETURN",    "u14",    "KP0",     "KP_DEL", "u15",    "u16",     "u17",
    "u18",       "SHF_L",  "EUR_K45", "Z",      "X",      "C",       "V",
    "B",         "N",      "M",       "COMMA",  "PERIOD", "VIRGUE",  "u19",
    "SHF_R",     "ARR_UP", "u20",     "NUMS",   "u21",    "u22",     "u23",
    "u24",       "CTRL_L", "WIN_L",   "ALT_L",  "u25",    "u26",     "u27",
    "SPACE",     "u28",    "u29",     "u30",    "ALT_R",  "FN1",     "APP",
    "",          "ARR_L",  "ARR_DW",  "ARR_R",  "CTRL_R", "u31",     "u32",
    "u33",       "u34"};

void pad_action_leftstick_x_positive(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbLX += scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_leftstick_x_negative(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbLX += -scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_leftstick_y_positive(double percent, bool inverse) {
  g_xinput_state.Gamepad.sThumbLY += scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_leftstick_y_negative(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbLY += -scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_rightstick_x_positive(double percent, bool inverse) {
  g_xinput_state.Gamepad.sThumbRX += scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_rightstick_x_negative(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbRX += -scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_rightstick_y_positive(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbRY += scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_rightstick_y_negative(double percent, bool inverse) {

  g_xinput_state.Gamepad.sThumbRY += -scale<SHORT>(percent * 0.5 + 0.5);
}

void pad_action_left_trigger_analog(double percent, bool inverse) {
  g_xinput_state.Gamepad.bLeftTrigger = scale<BYTE>(percent);
}

void pad_action_right_trigger_analog(double percent, bool inverse) {
  g_xinput_state.Gamepad.bRightTrigger = scale<BYTE>(percent);
}

void load_function_map() {
  pad_name_lookup_table.insert(
      std::make_pair("LStickX+", pad_action_leftstick_x_positive));
  pad_name_lookup_table.insert(
      std::make_pair("LStickX-", pad_action_leftstick_x_negative));
  pad_name_lookup_table.insert(
      std::make_pair("LStickY+", pad_action_leftstick_y_positive));
  pad_name_lookup_table.insert(
      std::make_pair("LStickY-", pad_action_leftstick_y_negative));
  pad_name_lookup_table.insert(
      std::make_pair("RStickX+", pad_action_rightstick_x_positive));
  pad_name_lookup_table.insert(
      std::make_pair("RStickX-", pad_action_rightstick_x_negative));
  pad_name_lookup_table.insert(
      std::make_pair("RStickY+", pad_action_rightstick_y_positive));
  pad_name_lookup_table.insert(
      std::make_pair("RStickY-", pad_action_rightstick_y_negative));
  pad_name_lookup_table.insert(
      std::make_pair("LTrigger", pad_action_left_trigger_analog));
  pad_name_lookup_table.insert(
      std::make_pair("RTrigger", pad_action_right_trigger_analog));
}

hid_device *open_target_device(struct hid_device_info *cur_dev) {
  for (; cur_dev; cur_dev = cur_dev->next) {
    if (cur_dev->vendor_id == 0x352D &&
        (cur_dev->product_id == 0x2383 || cur_dev->product_id == 0x2382 ||
         cur_dev->product_id == 0x2384) &&
        cur_dev->usage == 0x0) {
      printf("Device found: %s\n", cur_dev->path);
      return hid_open_path(cur_dev->path);
    }
  }
}

void key_height_handler(uint8_t keycode, double travel) {

  auto &action = g_key_action_map[keycode];
  if (action.f != nullptr) {
    action.f(travel, action.inverse);
    // if (travel > 0.2f)
    // spdlog::info("key: {} , height: {}, action: {:p}", keycode, travel,
    //              (void*) & action);
  } else {
    // spdlog::error("Action function pointer for key:{} is invalid!",
    //              keyboard_layout_a75[keycode]);
  }
}

void keyboard_identity_handler(unsigned char byte5, unsigned char byte6,
                               unsigned char byte7) {
  if (((byte5 == 11) && (byte6 == 1) && (byte7 == 1)) ||
      ((byte5 == 11) && (byte6 == 4) && (byte7 == 1))) {
    g_current_keyboard_identifier = 75;
  } else if ((byte5 == 11) && (byte6 == 4) && (byte7 == 3)) {
    g_current_keyboard_identifier = 750;
  } else if ((byte5 == 11) && (byte6 == 4) && (byte7 == 2)) {
    int current_iso = 751; // Assume UK as default
    if (current_iso == 751) {
      g_current_keyboard_identifier = 751;
    } else if (current_iso == 752) {
      g_current_keyboard_identifier = 752;
    } else if (current_iso == 753) {
      g_current_keyboard_identifier = 753;
    }
  } else if (((byte5 == 11) && (byte6 == 2) && (byte7 == 1)) ||
             ((byte5 == 15) && (byte6 == 1) && (byte7 == 1))) {
    g_current_keyboard_identifier = 65;
  } else if ((byte5 == 11) && (byte6 == 3) && (byte7 == 1)) {
    g_current_keyboard_identifier = 60;
  } else if ((byte5 == 11) && (byte6 == 4) && (byte7 == 1)) {
    g_current_keyboard_identifier = 82;
  } else if ((byte5 == 11) && (byte6 == 4) && (byte7 == 5)) {
    g_current_keyboard_identifier = 754;
  } else {
    g_current_keyboard_identifier = 0;
  }
}
std::string get_keyboard_name_from_id(int keyboard_type) {
  std::string keyboard_name;

  switch (keyboard_type) {

  case 75:
    keyboard_name = "A75";
    break;
  case 750:
    keyboard_name = "A75Pro";
    break;
  case 751:
    keyboard_name = "A75 ISO - UK";
    break;
  case 752:
    keyboard_name = "A75 ISO - FR";
    break;
  case 753:
    keyboard_name = "A75 ISO - DE";
    break;
  case 754:
    keyboard_name = "G75";
    break;
  case 65:
    keyboard_name = "G65";
    break;
  case 60:
    keyboard_name = "G60";
    break;
  case 82:
    keyboard_name = "K82";
    break;
  default:
    keyboard_name = "Unknown Keyboard";
    break;
  }

  return keyboard_name;
}
// packet functions
int sendpkt_request_keys() {
  const unsigned char buf[4] = {0x04, 0xb6, 0x03, 0x01};
  return hid_write(g_hid_device_handle, buf, 4);
}

int sendpkt_request_identity() {
  const unsigned char buf[3] = {0x04, 0xa0, 0x02};
  return hid_write(g_hid_device_handle, buf, 3);
}

/*

1. host -> keyboard request_keys

2. kb -> host keys*3

3. host -> keyboard ...



*/

// receive process
void receive_packet_controller(PVIGEM_CLIENT client, PVIGEM_TARGET pad,
                               unsigned char *buffer) {
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

  if (command == 0xa0) {
    if (byte2 == 0x02 && byte3 == 0x00) {
      // keyboard respond identify command
      keyboard_identity_handler(byte5, byte6, byte7);

      spdlog::info("Connected to keyboard: {}",
                   get_keyboard_name_from_id(g_current_keyboard_identifier));
      return;
    }
  }

  if (command != 0xb7) {
    // we only need key height message here
    return;
  }

  int base_position = 0;
  int operation_length = 0;
  if (byte4 == 0) {
    base_position = 0;
    operation_length = 59;
  } else if (byte4 == 1) {
    base_position = 59;
    operation_length = 59;
  } else {
    base_position = 118;
    operation_length = 8;
    should_send.store(true);
  }

  for (int i = 0; i < operation_length; i++) {

    int keynum = base_position + i;
    unsigned char new_value = buffer[i + 4];
    if (new_value < g_deadzone_min) {
      new_value = 0;
    }
    if (new_value > g_deadzone_max) {
      new_value = 40;
    }
    double p = new_value / 40.0;
  /*  if (int(new_value) >= 10) {
      std::cout << "key: " << keynum << "(" << keyboard_layout_a75[keynum]
                << ")"
                << " , height: " << int(new_value) << std::endl;
    }*/

    key_height_handler(keynum, p);
  }
  if (byte4 != 0x00 && byte4 != 0x01) {
    // spdlog::info("update: {}", g_xinput_state.Gamepad.sThumbLY);
    vigem_target_x360_update(
        client, pad, *reinterpret_cast<XUSB_REPORT *>(&g_xinput_state.Gamepad));
    g_xinput_state.Gamepad.sThumbLX = 0;
    g_xinput_state.Gamepad.sThumbLY = 0;
    g_xinput_state.Gamepad.sThumbRX = 0;
    g_xinput_state.Gamepad.sThumbRY = 0;
  }
}
// debug purpose
// void receive_packet_handler(char *buffer) {
//
//  // not our device
//  if (buffer[0] != 0x04) {
//    return;
//  }
//
//  unsigned char command = buffer[1];
//  unsigned char byte2 = buffer[2];
//  unsigned char byte3 = buffer[3];
//  unsigned char byte4 = buffer[4];
//  unsigned char byte5 = buffer[5];
//  unsigned char byte6 = buffer[6];
//  unsigned char byte7 = buffer[7];
//
//  switch (command) {
//  case 0xa0: {
//    if (byte2 == 0x02) {
//      if (byte3 == 0) {
//        // keyboard respond identify command
//        printf("keyboard:%d %d %d\n", byte5, byte6, byte7);
//        return;
//      }
//    }
//    break;
//  }
//
//  case 0xb7: {
//    int base_position = 0;
//    int operation_length = 0;
//    if (byte4 == 0) {
//      base_position = 0;
//      operation_length = 59;
//    } else if (byte4 == 1) {
//      base_position = 59;
//      operation_length = 59;
//    } else {
//      base_position = 118;
//      operation_length = 8;
//      std::this_thread::sleep_for(std::chrono::milliseconds(5));
//      sendpkt_request_keys();
//    }
//    for (int i = 0; i < operation_length; i++) {
//      unsigned char new_value = buffer[i + 4];
//      // std::cout << "key: " << base_position + i << " , height: " <<
//      // int(new_value) << std::endl;
//      if (new_value < 2) {
//        new_value = 0;
//      }
//      if (new_value != g_key_height_array[base_position + i]) {
//        g_key_height_array[base_position + i] = new_value;
//        std::cout << "key: " << base_position + i
//                  << " , height: " << new_value * 100.0 / 40.0 << std::endl;
//      }
//    }
//  }
//  default:
//    break;
//  }
//}

using namespace conmanip;

void ShowConsoleCursor(bool showFlag) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

  CONSOLE_CURSOR_INFO cursorInfo;

  GetConsoleCursorInfo(out, &cursorInfo);
  cursorInfo.bVisible = showFlag; // set the cursor visibility
  SetConsoleCursorInfo(out, &cursorInfo);
}
int16_t keyname_to_index(std::vector<std::string> v, std::string k) {
  auto it = find(v.begin(), v.end(), k);

  if (it != v.end()) {
    int16_t index = it - v.begin();
    return index;
  } else {
    spdlog::error("Invalid keyname: {}!", k);
    return -1;
  }
}

void register_key_action(std::string keyname, std::string padname,
                         bool inverse) {
  auto operation = pad_name_lookup_table[padname];
  int16_t keycode = keyname_to_index(keyboard_layout_a75, keyname);

  if (keycode == -1) {
    spdlog::error("failed to find keycode for {}, ignoring.", keyname);
    return;
  }

  pad_action act;
  act.f = operation;
  act.inverse = inverse;

  g_key_action_map.insert(std::make_pair(keycode, act));
  spdlog::info("Registered {}({}) to {}", keyboard_layout_a75[keycode], keycode,
               padname);
}

void initialize_action_map(nlohmann::json config) {
  try {
    g_polling_interval = config.at("PollingInterval");
    g_deadzone_min = config.at("DeadZoneMin");
    g_deadzone_max = config.at("DeadZoneMax");
    spdlog::info("Active deadzones: 0-{}, {}-40", g_deadzone_min,
                 g_deadzone_max);
    for (const auto &item : config["ActionMaps"]) {
      std::string key = item["Key"];
      std::string action = item["Action"];
      bool invert = item.at("Invert");
      register_key_action(key, action, invert);
    }
  } catch (const nlohmann::json::type_error &e) {
    spdlog::error("Error while loading configuration file! Details: {}",
                  e.what());
  }
  //register_key_action("W", "LStickY+", false);
  //register_key_action("ARR_DW", "LStickY-", false);
  //register_key_action("ARR_R", "RStickX+", false);
  //register_key_action("ARR_L", "RStickX-", false);
  // register_key_action("W", "RTrigger", false);
  // register_key_action("S", "LTrigger", false);
}

int main(int argc, char *argv[]) {
  ShowConsoleCursor(false);
  console_out_context ctxout;
  console_out conout(ctxout);
  spdlog::set_pattern("[%^%l%$] %v");
  // ===============================================
  // keyboard setup procedures
  devs = hid_enumerate(0x0, 0x0);
  // print_devices_with_descriptor(devs);
  int res;
  // Initialize the hidapi library
  res = hid_init();
  // Open the device
  g_hid_device_handle = open_target_device(devs);
  hid_free_enumeration(devs);
  if (!g_hid_device_handle) {
    spdlog::error("Unable to open device");
    hid_exit();
    return 1;
  }
  hid_set_nonblocking(g_hid_device_handle, 0);
  // end of keyboard setup procedures
  // ===============================================

  // ===============================================
  // vigem setup procedures
  spdlog::info("Allocating a new virtual xbox360 controller !");
  const auto client = vigem_alloc();

  if (client == nullptr) {
    spdlog::error("Failed allocating memory for xbox360 controller !");
    return -1;
  }
  const auto retval = vigem_connect(client);

  if (!VIGEM_SUCCESS(retval)) {
    spdlog::error("ViGEm Bus connection failed with error code: {}",
                  uint64_t(retval));
    return -1;
  }
  // new handle for virtual pad
  const auto pad = vigem_target_x360_alloc();
  // add the pad
  const auto pir = vigem_target_add(client, pad);

  if (!VIGEM_SUCCESS(pir)) {
    spdlog::error("vigem_target_add() failed with error code: {}",
                  uint64_t(retval));
    return -1;
  }
  // end of vigem setup procedures
  // ===============================================

  // ===============================================
  // Configuration file init

  try {
    std::ifstream f("config.json");
    load_function_map();
    nlohmann::json config = nlohmann::json::parse(f);
    initialize_action_map(config);
  } catch (const std::exception &e) {
    spdlog::error("Error while reading config file: \n{}", e.what());
  }

  // End of Configuration file init
  // ===============================================

  // check keyboard identifier
  if (g_current_keyboard_identifier == 0) {
    sendpkt_request_identity();
  }

  // initial packet used to trigger the loop
  sendpkt_request_keys();

  // dedicated thread for sending delayed packet request to the keyboard
  std::thread send_thread([]() {
    while (true) {
      if (should_send.load()) {
        sendpkt_request_keys();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(g_polling_interval));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  send_thread.detach();

  // read buffer
  unsigned char hid_readbuffer[65];
  while (true) {
    // main loop for reading device response
    res = hid_read(g_hid_device_handle, hid_readbuffer, sizeof(hid_readbuffer));
    if (res < 0) {
      printf("Unable to read(): %ls\n", hid_error(g_hid_device_handle));
      break;
    } else if (res > 0) {

      if (g_current_keyboard_identifier != 0) {
        // conout.setpos(0, 20);
        // std::cout << "sThumbLX:  " << g_xinput_state.Gamepad.sThumbLX
        //           << "        ";
        // conout.setpos(0, 21);
        // std::cout << "sThumbLY:  " << g_xinput_state.Gamepad.sThumbLY
        //           << "        ";
        // conout.setpos(0, 22);
        // std::cout << "sThumbRX:  " << g_xinput_state.Gamepad.sThumbRX
        //           << "        ";
        // conout.setpos(0, 23);
        // std::cout << "sThumbRY:  " << g_xinput_state.Gamepad.sThumbRY
        //           << "        ";
        // conout.setpos(0, 24);
        // std::cout << "bLeftTrigger:  "
        //           << int(g_xinput_state.Gamepad.bLeftTrigger) << "        ";
        // conout.setpos(0, 25);
        // std::cout << "bRightTrigger: "
        //           << int(g_xinput_state.Gamepad.bRightTrigger) << "        ";
      }

      receive_packet_controller(client, pad, hid_readbuffer);
    }
  }

  printf("Exiting...\n");
  hid_close(g_hid_device_handle);

  res = hid_exit();
  system("pause");
  return 0;
}