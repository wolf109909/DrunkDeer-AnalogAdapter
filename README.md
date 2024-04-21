# DrunkDeer-AnalogAdapter
 - A simple app that emulates an xbox360 controller, redirecting analog readings from drunkdeer keyboards.
# Supported Keyboards
 - It should support all DrunkDeer keyboards now!
# QuickStart
 - Download the latest ViGEmBus driver from https://github.com/nefarius/ViGEmBus/releases
 - Download latest release from github. (Releases should be on your right)
 - run the app
# Configurations
 - By default, the app binds WASD to the left stick on a xbox360 controller and Q E to analog LT and RT.
 - To customize analog input mapping, you need to edit the included `config.json` file. Json files are very strictly formatted. I recommend you using one of the [json formatting and validating tools](https://jsonlint.com/) if you don't know how Json works.
 - Each input map must consist of a `"Key"` and an `"Action"`. Just copy the format shown in `example.json` and you should be good to go.
 - Available keys:```"ESC","","","F1","F2","F3","F4","F5",
"F6","F7","F8","F9","F10","F11","F12",
"KP7","KP8","KP9","u1","u2","u3","u4",
"SWUNG","1","2","3","4","5","6",
"7","8","9","0","MINUS","PLUS","BACK",
"KP4","KP5","KP6","u5","u6","u7","u8",
"TAB","Q","W","E","R","T","Y",
"U","I","O","P","BRKTS_L","BRKTS_R","SLASH_K29",
"KP1","KP2","KP3","u9","u10","u11","u12",
"CAPS","A","S","D","F","G","H",
"J","K","L","COLON","QOTATN","u13","RETURN",
"u14","KP0","KP_DEL","u15","u16","u17","u18",
"SHF_L","EUR_K45","Z","X","C","V","B",
"N","M","COMMA","PERIOD","VIRGUE","u19","SHF_R",
"ARR_UP","u20","NUMS","u21","u22","u23","u24",
"CTRL_L","WIN_L","ALT_L","u25","u26","u27","SPACE",
"u28","u29","u30","ALT_R","FN1","APP","","ARR_L",
"ARR_DW","ARR_R","CTRL_R","u31","u32","u33","u34" ```
 - Available analog actions: ```"LStickX+","LStickX-","LStickY+","LStickY-","RStickX+","RStickX-","RStickY+","RStickY-","LTrigger","RTrigger" ```
# Todo
- Digital button mapping
# Development
To build this project, you need to have https://github.com/nefarius/ViGEmClient.vcpkg installed.
