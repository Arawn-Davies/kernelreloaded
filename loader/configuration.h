/* Copyright (c) 2007 Mega Man */
#ifndef _CONFIGURATION_H_
#define _CONFIGURATION_H_

/** FIle where configuration is stored. */
#define CONFIG_FILE CONFIG_DIR "/config.txt"
/** Directory where configuration is stored. */
#define CONFIG_DIR "mc0:kloader"
/** Directory where configuration is stored on second mc. */
#define CONFIG_DIR2 "mc1:kloader"
/** Configuration on the second memory card.
 *
 * CONFIG_DIR2 existed but had no matching file define, because nothing ever
 * loaded from the second card -- it was referenced only by saveMcIcons(). This
 * is the mc1 half of the startup search. */
#define CONFIG_FILE2 CONFIG_DIR2 "/config.txt"
/** Configuration served over host:, for development.
 *
 * ps2client (and PCSX2, which maps host: to the directory the ELF was loaded
 * from) can serve this without any memory card or USB stick present. Tried
 * last, and simply fails to open on a real console, so it costs nothing
 * there. */
#define HOST_CONFIG_FILE "host:config.txt"
/** Config file used for DVDs. */
#define DVD_CONFIG_FILE "cdfs:config.txt"
/** Config file used for NetSurf from USB storage device. */
#define PS2NS_CONFIG_FILE "mass0:PS2NS/CONFIG.TXT"
/** Config file from USB storage device. */
#define USB_CONFIG_FILE "mass0:CONFIG.TXT"

#ifdef __cplusplus
extern "C" {
#endif
int loadConfiguration(const char *configfile);
int loadConfigurationFromRom(const char *name);
/** ROM file name of the built-in fallback config; see loader/Makefile ROM_FILES. */
#define ROM_CONFIG_FILE "defaultconfig.txt"
void saveMcIcons(const char *config_dir);
void saveConfiguration(const char *configfile);
#ifdef __cplusplus
}
#endif
void addConfigTextItem(const char *name, char *value, int maxlen);
void addConfigCheckItem(const char *name, int *value);
void addConfigVideoItem(const char *name, int *value);

#endif
