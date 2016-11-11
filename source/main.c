/*
Copyright 2016 Seth VanHeulen

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"

#define QUEST_SIZE_SMALL 0x1400
#define QUEST_SIZE_LARGE 0x1C00
#define INSTALLED_OFFSET 0x34
#define QUEST_OFFSET 0x45D4

void get_current_firm(int* firm) {
    OS_VersionBin nver;
    OS_VersionBin cver;
    osGetSystemVersionData(&nver, &cver);
    bool is_new;
    APT_CheckNew3DS(&is_new);
    if (is_new)
        firm[0] = 0;
    else
        firm[0] = 1;
    firm[1] = cver.mainver;
    firm[2] = cver.minor;
    firm[3] = cver.build;
    firm[4] = nver.mainver;
    if (cver.region == 'J')
        firm[5] = 0;
    else if (cver.region == 'E')
        firm[5] = 1;
    else if (cver.region == 'U')
        firm[5] = 2;
    else if (cver.region == 'K')
        firm[5] = 3;
    return;
}

int select_game(const char* description, const char* info, FS_Archive* extdata, Handle* system) {
    ui_menu_entry game_menu[] = {
        {"Monster Hunter X (JPN)", 1},
        {"Monster Hunter Generations (EUR)", 1},
        {"Monster Hunter Generations (USA)", 1},
    };
    int game = ui_menu(description, game_menu, 3);
    ui_info_clear();
    if (game == -1)
        return -1;
    int path_data[3] = {MEDIATYPE_SD, 0x1554, 0};
    FS_Path path = {PATH_BINARY, 0xC, (char*) path_data};
    if (game == 1)
        path_data[1] = 0x185b;
    else if (game == 2)
        path_data[1] = 0x1870;
    if (FSUSER_OpenArchive(extdata, ARCHIVE_EXTDATA, path) != 0) {
        ui_pause("Error: Unable to open extdata");
        return -1;
    }
    if (FSUSER_OpenFile(system, *extdata, fsMakePath(PATH_ASCII, "/system"), FS_OPEN_WRITE, 0) != 0) {
        FSUSER_CloseArchive(*extdata);
        ui_pause("Error: Unable to open extdata:/system");
        return -1;
    }
    ui_info_add(info);
    ui_info_add("\n");
    ui_info_add("  ");
    ui_info_add(game_menu[game].text);
    ui_info_add("\n");
    return game;
}

int select_type(const char* description, const char* info) {
    ui_menu_entry type_menu[] = {
        {"Challenge Quest", 1},
        {"Event Quest", 1},
    };
    int type = ui_menu(description, type_menu, 2);
    if (type == -1) {
        ui_pause("Install canceled");
        return -1;
    }
    ui_info_add(info);
    ui_info_add("\n");
    ui_info_add("  ");
    ui_info_add(type_menu[type].text);
    ui_info_add("\n");
    return type;
}

int select_firm(const char* description, const char* info, int* firm) {
    if (ui_firm(description, firm) == 0) {
        ui_pause("Install canceled");
        return 0;
    }
    ui_info_add(info);
    ui_info_add("\n");
    ui_info_add("  ");
    char hardware[][8] = {"New 3DS", "Old 3DS"};
    char region[] = {'J', 'E', 'U', 'K'};
    char firm_string[50];
    sprintf(firm_string, "%s %d.%d.%d-%d%c\n", hardware[firm[0]], firm[1], firm[2], firm[3], firm[4], region[firm[5]]);
    ui_info_add(firm_string);
    return 1;
}

int install_otherapp(FS_Archive extdata, char* otherapp_buffer, int otherapp_size) {
    ui_info_add("Installing otherapp ... ");
    FS_Path otherapp_path = fsMakePath(PATH_ASCII, "/otherapp");
    FSUSER_DeleteFile(extdata, otherapp_path);
    if (FSUSER_CreateFile(extdata, otherapp_path, 0, otherapp_size) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to create extdata:/otherapp");
        return 0;
    }
    Handle otherapp_file;
    if (FSUSER_OpenFile(&otherapp_file, extdata, otherapp_path, FS_OPEN_WRITE, 0) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to open extdata:/otherapp");
        return 0;
    }
    u32 written;
    if (FSFILE_Write(otherapp_file, &written, 0, otherapp_buffer, otherapp_size, FS_WRITE_FLUSH) != 0 || written != otherapp_size) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to write to extdata:/otherapp");
        FSFILE_Close(otherapp_file);
        return 0;
    }
    FSFILE_Close(otherapp_file);
    ui_info_add("\x1b[32;1msuccess.\x1b[0m\n");
    return 1;
}

int download_otherapp(FS_Archive extdata, int* firm) {
    ui_info_add("Downloading otherapp ... ");
    if (httpcInit(0) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to initialize HTTP service");
        return 0;
    }
    char request_url[150];
    char hardware[][4] = {"NEW", "OLD"};
    char region[][4] = {"JPN", "EUR", "USA", "KOR"};
    sprintf(request_url, "http://smea.mtheall.com/get_payload.php?version=%s-%d-%d-%d-%d-%s", hardware[firm[0]], firm[1], firm[2], firm[3], firm[4], region[firm[5]]);
    httpcContext context;
    if (httpcOpenContext(&context, HTTPC_METHOD_GET, request_url, 1) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to open HTTP context");
        httpcExit();
        return 0;
    }
    int ret = 0;
    u32 status = 0;
    if (httpcAddRequestHeaderField(&context, "User-Agent", "genhax_installer") != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to add user agent HTTP header");
    } else if (httpcBeginRequest(&context) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to begin HTTP request");
    } else if (httpcGetResponseStatusCode(&context, &status) != 0 || status != 302) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to get HTTP status code or received\nerror status code");
    } else if (httpcGetResponseHeader(&context, "Location", request_url, 150) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to read location HTTP header");
    } else {
        httpcCloseContext(&context);
        if (httpcOpenContext(&context, HTTPC_METHOD_GET, request_url, 1) != 0) {
            ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
            ui_pause("Error: Unable to open HTTP context");
            httpcExit();
            return 0;
        }
        u32 otherapp_size;
        if (httpcAddRequestHeaderField(&context, "User-Agent", "genhax_installer") != 0) {
            ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
            ui_pause("Error: Unable to add user agent HTTP header");
        } else if (httpcBeginRequest(&context) != 0) {
            ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
            ui_pause("Error: Unable to begin HTTP request");
        } else if (httpcGetResponseStatusCode(&context, &status) != 0 || status != 200) {
            ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
            ui_pause("Error: Unable to get HTTP status code or received\nerror status code");
        } else if (httpcGetDownloadSizeState(&context, NULL, &otherapp_size) != 0) {
            ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
            ui_pause("Error: Unable to get response size");
        } else {
            char* otherapp_buffer = malloc(otherapp_size);
            if (otherapp_buffer == NULL) {
                ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
                ui_pause("Error: Unable to allocate memory for otherapp");
            } else if (httpcDownloadData(&context, (u8*) otherapp_buffer, otherapp_size, NULL) != 0) {
                ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
                ui_pause("Error: Unable to read response");
                free(otherapp_buffer);
            } else {
                ui_info_add("\x1b[32;1msuccess.\x1b[0m\n");
                ret = install_otherapp(extdata, otherapp_buffer, otherapp_size);
                free(otherapp_buffer);
            }
        }
    }
    httpcCloseContext(&context);
    httpcExit();
    return ret;
}

int install_quest(Handle system, char* file_path, int type, int large) {
    ui_info_add("Installing quest ... ");
    int section_offset;
    if (FSFILE_Read(system, NULL, 0xc, &section_offset, 4) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to read DLC section offset");
        return 0;
    }
    int quest_info[] = {type ? 1010001 : 1020001, 0};
    char quest_data[QUEST_SIZE_LARGE];
    FILE* quest = fopen(file_path, "rb");
    if (quest == NULL) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to open exploit quest");
        return 0;
    }
    quest_info[1] = fread(quest_data, 1, QUEST_SIZE_LARGE, quest);
    if (ferror(quest) || (quest_info[1] + 8) > (large ? QUEST_SIZE_LARGE : QUEST_SIZE_SMALL)) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to read exploit quest");
        fclose(quest);
        return 0;
    }
    fclose(quest);
    int quest_offset = section_offset + QUEST_OFFSET;
    if (type == 0)
        quest_offset += (large ? QUEST_SIZE_LARGE : QUEST_SIZE_SMALL) * 120;
    if (FSFILE_Write(system, NULL, quest_offset, quest_info, 8, FS_WRITE_FLUSH) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to write exploit quest info");
        return 0;
    }
    if (FSFILE_Write(system, NULL, quest_offset + 8, quest_data, quest_info[1], FS_WRITE_FLUSH) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to write exploit quest");
        return 0;
    }
    int installed_bits[] = {1, 0, 0, 0};
    if (FSFILE_Write(system, NULL, section_offset + INSTALLED_OFFSET + (type ? 4 : 0), installed_bits, (type ? 16 : 4), FS_WRITE_FLUSH) != 0) {
        ui_info_add("\x1b[31;1mfailure.\x1b[0m\n");
        ui_pause("Error: Unable to write installed quests info");
        return 0;
    }
    ui_info_add("\x1b[32;1msuccess.\x1b[0m\n");
    return 1;
}

int main(int argc, char* argv[]) {
    gfxInitDefault();
    ui_init();
    while (aptMainLoop()) {
        FS_Archive extdata;
        Handle system;
        int game = select_game("\x1b[32;1mGenHax Installer (v1.0.1)\x1b[0m\n\nSelect a game ...", "Selected game ...", &extdata, &system);
        char file_path[50] = "";
        if (game == -1)
            break;
        else if (game == 0)
            strcat(file_path, "JPN_");
        else if (game == 1)
            strcat(file_path, "EUR_");
        else if (game == 2)
            strcat(file_path, "USA_");
        int type = select_type("Select quest type ...\n\n\x1b[31;1mNote: You will no longer be able to post DLC\nquests of the type you select in the Hunters Hub\x1b[0m", "Selected quest type ...");
        if (type == -1) {
            FSFILE_Close(system);
            FSUSER_CloseArchive(extdata);
            continue;
        }
        else if (type == 0)
            strcat(file_path, "challenge.arc");
        else
            strcat(file_path, "event.arc");
        int firm[6];
        get_current_firm(firm);
        if (select_firm("Select firmware version ...", "Selected firmware version ...", firm) == 0) {
            FSFILE_Close(system);
            FSUSER_CloseArchive(extdata);
            continue;
        }
        if (download_otherapp(extdata, firm) && install_quest(system, file_path, type, game))
            ui_pause("Install completed successfully");
        FSFILE_Close(system);
        FSUSER_CloseArchive(extdata);
    }
    gfxExit();
    return 0;
}

