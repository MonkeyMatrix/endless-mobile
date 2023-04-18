/*
Copyright (c) 2023 by Rian Shelley

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DROPDOWN_H_
#define DROPDOWN_H_


#include "Rectangle.h"
#include <functional>
#include <vector>
#include <string>

/**
 * Handles drawing and input for a traditional dropdown control
 */
class Dropdown
{
public:
	virtual ~Dropdown() = default;

	void SetFontSize(int f) { font_size = f; }
	void SetPosition(const Rectangle& position) { this->position = position; }
	void SetOptions(const std::vector<std::string>& options);
	void SetSelected(const std::string& s);
	void SetSelectedIndex(int idx);
	const std::string& GetSelected() const { return selected_string; }
	int GetSelectedIndex() const { return selected_index; }

	bool MouseDown(int x, int y);
	bool MouseMove(int dx, int dy);
	bool MouseUp(int x, int y);
	bool Hover(int x, int y);

	bool MouseCaptured() const { return is_popped; }

	void Draw();

	enum ALIGN { LEFT, CENTER, RIGHT };
	void SetAlign(ALIGN a) { alignment = a; }
	void SetPadding(int p) { padding = p;}

	void SetEnabled(bool e) { enabled = e; }

	typedef std::function<void(int, const std::string&)> ChangedCallback;

	void SetCallback(ChangedCallback cb) { changed_callback = cb; }

private:
	int IdxFromPoint(int x, int y);


	Rectangle position;
	std::vector<std::string> options;
	std::string selected_string;
	int selected_index = -1;
	int highlight_index = -1;

	bool is_popped = false;
	bool is_hover = false;
	bool is_active = true;

	uint32_t click_stamp = 0;
	Point mouse_pos;

	int font_size = 18;
	ALIGN alignment = LEFT;
	int padding = 5;

	bool enabled = true;

	std::function<void(int, const std::string&)> changed_callback;
};

#endif