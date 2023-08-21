/* MainPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "MainPanel.h"

#include "BoardingPanel.h"
#include "Command.h"
#include "GamePad.h"
#include "RadialSelectionPanel.h"
#include "RingShader.h"
#include "comparators/ByGivenOrder.h"
#include "CategoryList.h"
#include "CoreStartData.h"
#include "Dialog.h"
#include "Interface.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "FrameTimer.h"
#include "GameData.h"
#include "Government.h"
#include "HailPanel.h"
#include "LineShader.h"
#include "MapDetailPanel.h"
#include "Messages.h"
#include "Mission.h"
#include "Phrase.h"
#include "Planet.h"
#include "PlanetPanel.h"
#include "PlayerInfo.h"
#include "PlayerInfoPanel.h"
#include "Preferences.h"
#include "Random.h"
#include "Screen.h"
#include "Ship.h"
#include "ShipEvent.h"
#include "SpriteSet.h"
#include "StellarObject.h"
#include "System.h"
#include "UI.h"

#include "opengl.h"

#include <cmath>
#include <sstream>
#include <string>

using namespace std;



MainPanel::MainPanel(PlayerInfo &player)
	: player(player), engine(player)
{
	SetIsFullScreen(true);
}



void MainPanel::Step()
{
	engine.Wait();

	// Depending on what UI element is on top, the game is "paused." This
	// checks only already-drawn panels.
	bool isActive = GetUI()->IsTop(this);

	// Display any requested panels.
	if(show.Has(Command::MAP))
	{
		GetUI()->Push(new MapDetailPanel(player));
		isActive = false;
	}
	else if(show.Has(Command::INFO))
	{
		GetUI()->Push(new PlayerInfoPanel(player));
		isActive = false;
	}
	else if(show.Has(Command::HAIL))
		isActive = !ShowHailPanel();
	show = Command::NONE;

	// If the player just landed, pop up the planet panel. When it closes, it
	// will call this object's OnCallback() function;
	if(isActive && player.GetPlanet() && !player.GetPlanet()->IsWormhole())
	{
		GetUI()->Push(new PlanetPanel(player, bind(&MainPanel::OnCallback, this)));
		player.Land(GetUI());
		// Save on landing, in case the app is killed uncleanly
		player.Save();
		isActive = false;
	}

	// Display any relevant help/tutorial messages.
	const Ship *flagship = player.Flagship();
	if(flagship)
	{
		// Check if any help messages should be shown.
		if(isActive && Preferences::Has("Control ship with mouse"))
			isActive = !DoHelp("control ship with mouse");
		if(isActive && flagship->IsTargetable())
			isActive = !DoHelp("navigation");
		if(isActive && flagship->IsDestroyed())
			isActive = !DoHelp("dead");
		if(isActive && flagship->IsDisabled() && !flagship->IsDestroyed())
			isActive = !DoHelp("disabled");
		bool canRefuel = player.GetSystem()->HasFuelFor(*flagship);
		if(isActive && !flagship->IsHyperspacing() && !flagship->JumpsRemaining() && !canRefuel)
			isActive = !DoHelp("stranded");
		shared_ptr<Ship> target = flagship->GetTargetShip();
		if(isActive && target && target->IsDisabled() && !target->GetGovernment()->IsEnemy())
			isActive = !DoHelp("friendly disabled");
		if(isActive && player.Ships().size() > 1)
			isActive = !DoHelp("multiple ship controls");
		if(isActive && flagship->IsTargetable() && player.Ships().size() > 1)
			isActive = !DoHelp("fleet harvest tutorial");
		if(isActive && flagship->IsTargetable() &&
				flagship->Attributes().Get("asteroid scan power") &&
				player.Ships().size() > 1)
			isActive = !DoHelp("fleet asteroid mining") && !DoHelp("fleet asteroid mining shortcuts");
		if(isActive && player.DisplayCarrierHelp())
			isActive = !DoHelp("try out fighters transfer cargo");
		if(isActive && Preferences::Has("Fighters transfer cargo"))
			isActive = !DoHelp("fighters transfer cargo");
		if(isActive && !flagship->IsHyperspacing() && flagship->Position().Length() > 10000.
				&& player.GetDate() <= player.StartData().GetDate() + 4)
		{
			++lostness;
			int count = 1 + lostness / 3600;
			if(count > lostCount && count <= 7)
			{
				string message = "lost 1";
				message.back() += lostCount;
				++lostCount;

				isActive = !DoHelp(message);
			}
		}
	}

	engine.Step(isActive);

	// Splice new events onto the eventQueue for (eventual) handling. No
	// other classes use Engine::Events() after Engine::Step() completes.
	eventQueue.splice(eventQueue.end(), engine.Events());
	// Handle as many ShipEvents as possible (stopping if no longer active
	// and updating the isActive flag).
	StepEvents(isActive);

	if(isActive)
		engine.Go();
	else
		canDrag = false;
	canClick = isActive;
}



void MainPanel::Draw()
{
	FrameTimer loadTimer;
	glClear(GL_COLOR_BUFFER_BIT);

	engine.Draw();

	if(isDragging)
	{
		if(canDrag)
		{
			const Color &dragColor = *GameData::Colors().Get("drag select");
			LineShader::Draw(dragSource, Point(dragSource.X(), dragPoint.Y()), .8f, dragColor);
			LineShader::Draw(Point(dragSource.X(), dragPoint.Y()), dragPoint, .8f, dragColor);
			LineShader::Draw(dragPoint, Point(dragPoint.X(), dragSource.Y()), .8f, dragColor);
			LineShader::Draw(Point(dragPoint.X(), dragSource.Y()), dragSource, .8f, dragColor);
		}
		else
			isDragging = false;
	}

	if(Preferences::Has("Show CPU / GPU load"))
	{
		string loadString = to_string(lround(load * 100.)) + "% GPU";
		const Color &color = *GameData::Colors().Get("medium");
		FontSet::Get(14).Draw(loadString, Point(10., Screen::Height() * -.5 + 5.), color);

		loadSum += loadTimer.Time();
		if(++loadCount == 60)
		{
			load = loadSum;
			loadSum = 0.;
			loadCount = 0;
		}
	}

	bool isActive = (GetUI()->Top().get() == this);
	if (isActive && Preferences::Has("Show buttons on map"))
	{
		Information info;
		const Interface *mapInterface = GameData::Interfaces().Get("map");
		const Interface *mapButtonUi = GameData::Interfaces().Get("main buttons");
		if(player.MapZoom() >= static_cast<int>(mapInterface->GetValue("max zoom")))
			info.SetCondition("max zoom");
		if(player.MapZoom() <= static_cast<int>(mapInterface->GetValue("min zoom")))
			info.SetCondition("min zoom");
		if(player.Flagship())
		{
			if (player.Flagship()->GetTargetStellar())
			{
				info.SetCondition("can hail");
			}

			bool hasFighters = false;
			bool hasReservedFighters = false;
			for (auto &ship: player.Ships())
			{
				if (ship->CanBeCarried() && !ship->IsParked() && !ship->IsDestroyed())
				{
					hasFighters = true;

					if (!(ship->HasDeployOrder()))
					{
						hasReservedFighters = true;
						break; // found the reserve, no need to look further
					}
				}
			}
			if (hasFighters)
			{
				if (hasReservedFighters)
				{
					info.SetCondition("can deploy");
				}
				else
				{
					info.SetCondition("can recall");
				}
			}

			if (player.Flagship()->GetTargetShip())
			{
				info.SetCondition("can hail");
				info.SetCondition("can scan");
				if (!player.Flagship()->GetTargetShip()->IsYours())
				{
					info.SetCondition("can attack");
				}
			}
			else if (player.Flagship()->GetTargetAsteroid())
			{
				info.SetCondition("targeting asteroid");
			}
			else if (player.Flagship()->Attributes().Get("cloak"))
			{
				info.SetCondition("can cloak");
			}

			bool hasSecondaryWeapon = false;
			for (auto& outfit: player.Flagship()->Outfits())
			{
				if (outfit.first->Icon())
				{
					hasSecondaryWeapon = true;
					break;
				}
			}
			if (hasSecondaryWeapon)
			{
				// Set the conditions for the interface to draw the fire button, and
				// custom draw the missile icon in the provided rect.
				info.SetCondition("has secondary");
				Rectangle icon_box = mapButtonUi->GetBox("ammo icon");
				auto& selectedWeapons = player.SelectedSecondaryWeapons();
				// The weapons selection cycle goes through three states:
				// 1. no weapons selected
				// 2. one weapon selected. Each selection gets the next weapon in sequence
				// 3. All weapons selected. It will fire all secondary weapons at once.
				if (selectedWeapons.empty())
				{
					SpriteShader::Draw(SpriteSet::Get("icon/none"), icon_box.Center());
				}
				else if (selectedWeapons.size() == 1)
				{
					info.SetCondition("secondary selected");
					SpriteShader::Draw((*selectedWeapons.begin())->Icon(), icon_box.Center());
				}
				else
				{
					info.SetCondition("secondary selected");
					SpriteShader::Draw(SpriteSet::Get("icon/all"), icon_box.Center());
				}
			}
		}

		mapButtonUi->Draw(info, this);

		// Draw a onscreen joystick in the bottom left corner, if enabled
		if(Preferences::Has("Onscreen Joystick"))
		{
			Rectangle scBounds = mapButtonUi->GetBox("onscreen joystick");
			const char* colorStr = "faint";
			if(osJoystick)
				colorStr = joystickMax ? "dim" : "dimmer";
			const Color &color = *GameData::Colors().Get(colorStr);
			RingShader::Draw(scBounds.Center(), scBounds.Width()/2, joystickMax ? 4.0 : 2.0, 1.0, color);

			if(osJoystick)
			{
				RingShader::Draw(osJoystick, 50, 0, color);
			}
		}
	}
}



// The planet panel calls this when it closes.
void MainPanel::OnCallback()
{
	engine.Place();
	// Run one step of the simulation to fill in the new planet locations.
	engine.Go();
	engine.Wait();
	engine.Step(true);
	// Start the next step of the simulation because Step() above still
	// thinks the planet panel is up and therefore will not start it.
	engine.Go();
}



// The hail panel calls this when it closes.
void MainPanel::OnBribeCallback(const Government *bribed)
{
	engine.BreakTargeting(bribed);
}



bool MainPanel::AllowsFastForward() const noexcept
{
	return true;
}



// Only override the ones you need; the default action is to return false.
bool MainPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(command.Has(Command::MAP | Command::INFO | Command::HAIL))
		show = command;
	else if(command.Has(Command::AMMO))
	{
		Preferences::ToggleAmmoUsage();
		Messages::Add("Your escorts will now expend ammo: " + Preferences::AmmoUsage() + "."
			, Messages::Importance::High);
	}
	else if((key == SDLK_MINUS || key == SDLK_KP_MINUS) && !command)
		Preferences::ZoomViewOut();
	else if((key == SDLK_PLUS || key == SDLK_KP_PLUS || key == SDLK_EQUALS) && !command)
		Preferences::ZoomViewIn();
	else if(key >= '0' && key <= '9' && !command)
		engine.SelectGroup(key - '0', mod & KMOD_SHIFT, mod & (KMOD_CTRL | KMOD_GUI));
	else
		return false;

	return true;
}



// Forward the given TestContext to the Engine under MainPanel.
void MainPanel::SetTestContext(TestContext &testContext)
{
	engine.SetTestContext(testContext);
}



bool MainPanel::Click(int x, int y, int clicks)
{
	// Don't respond to clicks if another panel is active.
	if(!canClick)
		return true;

	
	if(x > -100 && x < 100 && y > -100 && y < 100)
	{
		auto selection = new RadialSelectionPanel();
		selection->ReleaseWithMouseUp(Point(x, y), 1);
		selection->AddOption("ui/up_button", "Up Button", []() { Messages::Add("user clicked \"Up Button\""); });
		selection->AddOption("ui/right_button", "Right Button", []() { Messages::Add("user clicked \"Right Button\""); });
		selection->AddOption("ui/down_button", "Down Button", []() { Messages::Add("user clicked \"Down Button\""); });
		selection->AddOption("ui/left_button", "Left Button", []() { Messages::Add("user clicked \"Left Button\""); });
		GetUI()->Push(selection);
		return true;
	}


	// Only allow drags that start when clicking was possible.
	canDrag = true;

	dragSource = Point(x, y);
	dragPoint = dragSource;

	SDL_Keymod mod = SDL_GetModState();
	hasShift = (mod & KMOD_SHIFT);
	hasControl = (mod & KMOD_CTRL);

	engine.Click(dragSource, dragSource, hasShift, hasControl);

	return true;
}



bool MainPanel::RClick(int x, int y)
{
	engine.RClick(Point(x, y));

	return true;
}



bool MainPanel::Drag(double dx, double dy)
{
	if(!canDrag)
		return true;

	dragPoint += Point(dx, dy);
	isDragging = true;
	return true;
}



bool MainPanel::Release(int x, int y)
{
	if(isDragging)
	{
		dragPoint = Point(x, y);
		if(dragPoint.Distance(dragSource) > 5.)
			engine.Click(dragSource, dragPoint, hasShift, hasControl);

		isDragging = false;
	}

	return true;
}



bool MainPanel::Scroll(double dx, double dy)
{
	if(dy < 0)
		Preferences::ZoomViewOut();
	else if(dy > 0)
		Preferences::ZoomViewIn();
	else
		return false;

	return true;
}



bool MainPanel::FingerDown(int x, int y, int fid)
{
	// Don't respond to clicks if another panel is active.
	if(!canClick)
		return false;

	// If the gui is active, check for input
	bool isActive = (GetUI()->Top().get() == this);
	if (isActive)
	{
		// Check for onscreen joystick
		if(Preferences::Has("Onscreen Joystick") && osJoystickFinger == -1)
		{
			const Interface *mapButtonUi = GameData::Interfaces().Get("main buttons");

			Rectangle scBounds = mapButtonUi->GetBox("onscreen joystick");
			Point pring(
				x - scBounds.Center().X(),
				y - scBounds.Center().Y()
			);
			int radius = scBounds.Width()/2;

			// Are we within the ring?
			if(pring.LengthSquared() < radius * radius)
			{
				osJoystick = Point(x, y);
				joystickMax = false;
				osJoystickFinger = fid;

				Ship* flagship = player.Flagship();
				if(flagship)
				{
					flagship->SetMoveToward(pring * 5);
					Command::InjectSet(Command::MOVETOWARD);
				}
				return true;
			}
		}

		// Check for zoom events
		if(zoomGesture.FingerDown(Point(x, y), fid))
		{
			return true;
		}
	}


	return engine.FingerDown(Point(x, y), fid);
}



bool MainPanel::FingerMove(int x, int y, int fid)
{
	if (!canClick)
		return false;

	if(osJoystick && fid == osJoystickFinger)
	{
		const Interface *mapButtonUi = GameData::Interfaces().Get("main buttons");
		Rectangle scBounds = mapButtonUi->GetBox("onscreen joystick");

		// Don't let the point leave the bounds of the ring
		Point pring(
			x - scBounds.Center().X(),
			y - scBounds.Center().Y()
		);
		int radius = scBounds.Width()/2;

		// Are we outside the ring?
		float distance = pring.Length();
		if(distance > radius)
		{
			osJoystick = scBounds.Center() + pring * (radius / distance);
			if(!joystickMax)
			{
				joystickMax = true;
				Command::InjectSet(Command::AFTERBURNER);
			}
		}
		else
		{
			osJoystick = Point(x, y);
			if(joystickMax)
			{
				joystickMax = false;
				Command::InjectUnset(Command::AFTERBURNER);
			}
		}

		Ship* flagship = player.Flagship();
		if(flagship)
		{
			flagship->SetMoveToward(pring * 5);
		}

		return true;
	}
	else if(zoomGesture.FingerMove(Point(x, y), fid))
	{
		Preferences::ZoomView(zoomGesture.Zoom());
		return true;
	}

	return engine.FingerMove(Point(x, y), fid);
}



bool MainPanel::FingerUp(int x, int y, int fid)
{
	if(osJoystick && fid == osJoystickFinger)
	{
		osJoystick = Point();
		joystickMax = false;
		osJoystickFinger = -1;
		Command::InjectUnset(Command::MOVETOWARD);
		Command::InjectUnset(Command::AFTERBURNER);
		return true;
	}
	else if(zoomGesture.FingerUp(Point(x, y), fid))
	{
		return true;
	}
	return engine.FingerUp(Point(x, y), fid);
}



bool MainPanel::ControllerAxis(SDL_GameControllerAxis axis, int position)
{
	if(axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY)
	{
		// TODO: combine this logic with the onscreen joystick logic?
		Ship* flagship = player.Flagship();
		if(flagship)
		{
			Point p = GamePad::LeftStick();

			if(p)
			{
				Command::InjectSet(Command::MOVETOWARD);
			
				flagship->SetMoveToward(p);

				if(p.LengthSquared() > 30000*30000)
				{
					if(!joystickMax)
					{
						joystickMax = true;
						Command::InjectSet(Command::AFTERBURNER);
					}
				}
				else if(joystickMax)
				{
					joystickMax = false;
					Command::InjectUnset(Command::AFTERBURNER);
				}
			}
			else
			{
				// joystick has returned to zero position
				Command::InjectUnset(Command::MOVETOWARD);
				Command::InjectUnset(Command::AFTERBURNER);
				joystickMax = false;
			}
		}
		return true;
	}
	return false;
}



//bool MainPanel::ControllerTriggerPressed(SDL_GameControllerAxis axis, bool positive)
//{
//	if(axis == SDL_CONTROLLER_AXIS_RIGHTY)
//	{
//		// TODO: you have to press and release this multiple times to trigger,
//		// but it feels like it should just keep zooming as long as you hold down
//		// the stick.
//		if(positive)
//			Preferences::ZoomViewOut();
//		else
//			Preferences::ZoomViewIn();
//		return true;
//	}
//	return false;
//}



void MainPanel::ShowScanDialog(const ShipEvent &event)
{
	shared_ptr<Ship> target = event.Target();

	ostringstream out;
	if(event.Type() & ShipEvent::SCAN_CARGO)
	{
		bool first = true;
		for(const auto &it : target->Cargo().Commodities())
			if(it.second)
			{
				if(first)
					out << "This " + target->Noun() + " is carrying:\n";
				first = false;

				out << "\t" << Format::CargoString(it.second, it.first) << "\n";
			}
		for(const auto &it : target->Cargo().Outfits())
			if(it.second)
			{
				if(first)
					out << "This " + target->Noun() + " is carrying:\n";
				first = false;

				out << "\t" << it.second;
				if(it.first->Get("installable") < 0.)
				{
					int tons = ceil(it.second * it.first->Mass());
					out << Format::CargoString(tons, Format::LowerCase(it.first->PluralName())) << "\n";
				}
				else
					out << " " << (it.second == 1 ? it.first->DisplayName(): it.first->PluralName()) << "\n";
			}
		if(first)
			out << "This " + target->Noun() + " is not carrying any cargo.\n";
	}
	if((event.Type() & ShipEvent::SCAN_OUTFITS) && target->Attributes().Get("inscrutable"))
		out << "Your scanners cannot make any sense of this " + target->Noun() + "'s interior.";
	else if(event.Type() & ShipEvent::SCAN_OUTFITS)
	{
		if(!target->Outfits().empty())
			out << "This " + target->Noun() + " is equipped with:\n";
		else
			out << "This " + target->Noun() + " is not equipped with any outfits.\n";

		// Split target->Outfits() into categories, then iterate over them in order.
		vector<string> categories;
		for(const auto &category : GameData::GetCategory(CategoryType::OUTFIT))
			categories.push_back(category.Name());
		auto comparator = ByGivenOrder<string>(categories);
		map<string, map<const string, int>, ByGivenOrder<string>> outfitsByCategory(comparator);
		for(const auto &it : target->Outfits())
		{
			string outfitNameForDisplay = (it.second == 1 ? it.first->DisplayName() : it.first->PluralName());
			outfitsByCategory[it.first->Category()].emplace(std::move(outfitNameForDisplay), it.second);
		}
		for(const auto &it : outfitsByCategory)
		{
			if(it.second.empty())
				continue;

			// Print the category's name and outfits in it.
			out << "\t" << (it.first.empty() ? "Unknown" : it.first) << "\n";
			for(const auto &it2 : it.second)
				if(!it2.first.empty() && it2.second > 0)
					out << "\t\t" << it2.second << " " << it2.first << "\n";
		}

		map<string, int> count;
		for(const Ship::Bay &bay : target->Bays())
			if(bay.ship)
			{
				int &value = count[bay.ship->ModelName()];
				if(value)
				{
					// If the name and the plural name are the same string, just
					// update the count. Otherwise, clear the count for the
					// singular name and set it for the plural.
					int &pluralValue = count[bay.ship->PluralModelName()];
					if(!pluralValue)
					{
						value = -1;
						pluralValue = 1;
					}
					++pluralValue;
				}
				else
					++value;
			}
		if(!count.empty())
		{
			out << "This " + target->Noun() + " is carrying:\n";
			for(const auto &it : count)
				if(it.second > 0)
					out << "\t" << it.second << " " << it.first << "\n";
		}
	}
	GetUI()->Push(new Dialog(out.str()));
}



bool MainPanel::ShowHailPanel()
{
	// An exploding ship cannot communicate.
	const Ship *flagship = player.Flagship();
	if(!flagship || flagship->IsDestroyed())
		return false;

	// Player cannot hail while landing / departing.
	if(flagship->Zoom() < 1.)
		return false;

	shared_ptr<Ship> target = flagship->GetTargetShip();
	if((SDL_GetModState() & KMOD_SHIFT) && flagship->GetTargetStellar())
		target.reset();

	if(flagship->IsEnteringHyperspace())
		Messages::Add("Unable to send hail: your flagship is entering hyperspace.", Messages::Importance::High);
	else if(flagship->Cloaking() == 1.)
		Messages::Add("Unable to send hail: your flagship is cloaked.", Messages::Importance::High);
	else if(target)
	{
		// If the target is out of system, always report a generic response
		// because the player has no way of telling if it's presently jumping or
		// not. If it's in system and jumping, report that.
		if(target->Zoom() < 1. || target->IsDestroyed() || target->GetSystem() != player.GetSystem()
				|| target->Cloaking() == 1.)
			Messages::Add("Unable to hail target " + target->Noun() + ".", Messages::Importance::High);
		else if(target->IsEnteringHyperspace())
			Messages::Add("Unable to send hail: " + target->Noun() + " is entering hyperspace."
				, Messages::Importance::High);
		else
		{
			GetUI()->Push(new HailPanel(player, target,
				[&](const Government *bribed) { MainPanel::OnBribeCallback(bribed); }));
			return true;
		}
	}
	else if(flagship->GetTargetStellar())
	{
		const Planet *planet = flagship->GetTargetStellar()->GetPlanet();
		if(!planet)
			Messages::Add("Unable to send hail.", Messages::Importance::High);
		else if(planet->IsWormhole())
		{
			static const Phrase *wormholeHail = GameData::Phrases().Get("wormhole hail");
			Messages::Add(wormholeHail->Get(), Messages::Importance::High);
		}
		else if(planet->IsInhabited())
		{
			GetUI()->Push(new HailPanel(player, flagship->GetTargetStellar()));
			return true;
		}
		else
			Messages::Add("Unable to send hail: " + planet->Noun() + " is not inhabited."
				, Messages::Importance::High);
	}
	else
		Messages::Add("Unable to send hail: no target selected.", Messages::Importance::High);

	return false;
}



// Handle ShipEvents from this and previous Engine::Step calls. Start with the
// oldest and then process events until any create a new UI element.
void MainPanel::StepEvents(bool &isActive)
{
	while(isActive && !eventQueue.empty())
	{
		const ShipEvent &event = eventQueue.front();
		const Government *actor = event.ActorGovernment();

		// Pass this event to the player, to update conditions and make
		// any new UI elements (e.g. an "on enter" dialog) from their
		// active missions.
		if(!handledFront)
			player.HandleEvent(event, GetUI());
		handledFront = true;
		isActive = (GetUI()->Top().get() == this);

		// If we can't safely display a new UI element (i.e. an active
		// mission created a UI element), then stop processing events
		// until the current Conversation or Dialog is resolved. This
		// will keep the current event in the queue, so we can still
		// check it for various special cases involving the player.
		if(!isActive)
			break;

		// Handle boarding events.
		// 1. Boarding an NPC may "complete" it (i.e. "npc board"). Any UI element that
		// completion created has now closed, possibly destroying the event target.
		// 2. Boarding an NPC may create a mission (e.g. it thanks you for the repair/refuel,
		// asks you to complete a quest, bribes you into leaving it alone, or silently spawns
		// hostile ships). If boarding creates a mission with an "on offer" conversation, the
		// ConversationPanel will only let the player plunder a hostile NPC if the mission is
		// declined or deferred - an "accept" is assumed to have bought the NPC its life.
		// 3. Boarding a hostile NPC that does not display a mission UI element will display
		// the BoardingPanel, allowing the player to plunder it.
		const Ship *flagship = player.Flagship();
		if((event.Type() & (ShipEvent::BOARD | ShipEvent::ASSIST)) && actor->IsPlayer()
				&& !event.Target()->IsDestroyed() && flagship && event.Actor().get() == flagship)
		{
			auto boardedShip = event.Target();
			Mission *mission = player.BoardingMission(boardedShip);
			if(mission && mission->HasSpace(*flagship))
				mission->Do(Mission::OFFER, player, GetUI(), boardedShip);
			else if(mission)
				player.HandleBlockedMissions((event.Type() & ShipEvent::BOARD)
						? Mission::BOARDING : Mission::ASSISTING, GetUI());
			// Determine if a Dialog or ConversationPanel is being drawn next frame.
			isActive = (GetUI()->Top().get() == this);

			// Confirm that this event's target is not destroyed and still an
			// enemy before showing the BoardingPanel (as a mission NPC's
			// completion conversation may have allowed it to be destroyed or
			// captured).
			// TODO: This BoardingPanel should not be displayed if a mission NPC
			// completion conversation creates a BoardingPanel for it, or if the
			// NPC completion conversation ends via `accept,` even if the ship is
			// still hostile.
			if(isActive && (event.Type() == ShipEvent::BOARD) && !boardedShip->IsDestroyed()
					&& boardedShip->GetGovernment()->IsEnemy())
			{
				// Either no mission activated, or the one that did was "silent."
				GetUI()->Push(new BoardingPanel(player, boardedShip));
				isActive = false;
			}
		}

		// Handle scan events of or by the player.
		if(event.Type() & (ShipEvent::SCAN_CARGO | ShipEvent::SCAN_OUTFITS))
		{
			if(actor->IsPlayer())
			{
				ShowScanDialog(event);
				isActive = false;
			}
			else if(event.TargetGovernment() && event.TargetGovernment()->IsPlayer())
			{
				string message = actor->Fine(player, event.Type(), &*event.Target());
				if(!message.empty())
				{
					GetUI()->Push(new Dialog(message));
					isActive = false;
				}
			}
		}

		// Remove the fully-handled event.
		eventQueue.pop_front();
		handledFront = false;
	}
}
