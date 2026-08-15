/* Copyright (c) 2007 Mega Man */
#include "menu.h"
#include "graphic.h"
#include "configuration.h"
#include "font.h"

void Menu::paint(void)
{
	int y, x;
	int z;
	std::vector<MenuEntry>::iterator i;
	int start;
	int c;

	y = positionY;
	x = positionX;

	if (title != NULL) {
		/* Menu heading, in the atlas face so it belongs to the same typographic
		 * family as the entries below it rather than to the BIOS ROM font. Its
		 * own scale, between the entries and the title lockup, and a rule under
		 * it to separate the heading from the list without a wide empty gap. */
		static u64 TexCol;
		static u64 TexRule;
		int w;

		TexCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
		TexRule = GS_SETREG_RGBAQ(0x4F, 0xB4, 0xF0, 0x40, 0x00);

		fontPrint(xoffset + x, yoffset + y, 3, TexCol, title, FONT_TITLE);
		w = fontMeasure(title, FONT_TITLE);
		y += fontLineHeight(FONT_TITLE) + 3;

		/* Rule the width of the heading, in the lockup's blue. Measured, so it
		 * tracks the title rather than being a guessed constant. */
		gsKit_prim_sprite(gsGlobal, xoffset + x, yoffset + y,
			xoffset + x + w, yoffset + y + 1, 3, TexRule);
		y += 8;
	}

	start = 0;
	if (numberOfMenuEntries > numberOfMenuItems) {
		if ((numberOfMenuEntries - start) < numberOfMenuItems) {
			start = numberOfMenuEntries - numberOfMenuItems;
		} else {
			start = selectedMenu - (numberOfMenuItems / 2);
			if (start < 0) {
				start = 0;
			}
		}
	}
	c = 0;
	z = 2;
	for (i = menuVector.begin(); i != menuVector.end(); i++) {
		if (c >= start) {
			i->paint(x, y, z);
			y += 26;	/* item pitch, scaled with the item text */
			z += 2;
		}
		c++;
		if ((c - start) >= numberOfMenuItems) {
			break;
		}
	}
}

int i;

void Menu::addItem(const char *name, executeMenuFn_t *executeFn, void *executeArg, GSTEXTURE *tex)
{
	MenuEntry menuEntry(gsGlobal, gsFont, name, executeFn, executeArg, tex);
	menuVector.push_back(menuEntry);

	selectMenuEntry(selectedMenu);

	numberOfMenuEntries++;
}

int checkItem(void *arg)
{
	MenuEntry *menuEntry = (MenuEntry *) arg;

	menuEntry->switchItem();

	return 0;
}

void Menu::addCheckItem(const char *name, int *value)
{
	MenuEntry menuEntry(gsGlobal, gsFont, name, checkItem, value);
	menuVector.push_back(menuEntry);

	selectMenuEntry(selectedMenu);
	addConfigCheckItem(name, value);

	numberOfMenuEntries++;
}

void Menu::addMultiSelectionItem(const char *name, const char **valueList, int *value, GSTEXTURE *tex)
{
	MenuEntry menuEntry(gsGlobal, gsFont, valueList, checkItem, value, tex);
	menuVector.push_back(menuEntry);

	selectMenuEntry(selectedMenu);
	addConfigCheckItem(name, value);

	numberOfMenuEntries++;
}


void Menu::selectMenuEntry(int selection)
{
	menuVector[selectedMenu].setSelected(false);
	selectedMenu = selection;
	menuVector[selectedMenu].setSelected(true);
}

int Menu::execute(void)
{
	return menuVector[selectedMenu].execute();
}

Menu *Menu::addSubMenu(const char *name)
{
	Menu *subMenu;

	subMenu = new Menu(gsGlobal, gsFont, numberOfMenuItems);
	subMenu->setTitle(name);
	subMenu->setPosition(positionX, positionY);
	subMenu->setParent(this);

	addItem(name, setCurrentMenu, subMenu);

	subMenuVector.push_back(subMenu);

	return subMenu;
}

Menu *Menu::getSubMenu(const char *name)
{
	Menu *subMenu;

	subMenu = new Menu(gsGlobal, gsFont, numberOfMenuItems);
	subMenu->setTitle(name);
	subMenu->setPosition(positionX, positionY);

	subMenuVector.push_back(subMenu);

	return subMenu;
}
void Menu::reset(GSGLOBAL *gsGlobal, GSFONTM *gsFont, int numberOfMenuItems)
{
	std::vector<MenuEntry>::iterator i;
	std::vector<Menu *>::iterator n;

	this->gsGlobal = gsGlobal;
	this->gsFont = gsFont;
	this->numberOfMenuItems = numberOfMenuItems;
	for (i = menuVector.begin(); i != menuVector.end(); i++) {
		i->reset(gsGlobal, gsFont);
	}
	for (n = subMenuVector.begin(); n != subMenuVector.end(); n++) {
		(*n)->reset(gsGlobal, gsFont, numberOfMenuItems);
	}
}
