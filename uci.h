/*
*/

#ifndef UCI_H
#define UCI_H

#include <map>

#include "houdini.h"

class Position;

namespace UCI
{
	class UciOption;

	struct CaseInsensitiveLess
	{
		bool operator() (const std::string&, const std::string&) const;
	};

	typedef std::map<std::string, UciOption, CaseInsensitiveLess> UciOptionsMap;

	class UciOption
	{
		typedef void(*OnChange)(const UciOption&);

	public:
		UciOption(OnChange = nullptr);
		UciOption(bool v, OnChange = nullptr);
		UciOption(const char* v, OnChange = nullptr);
		UciOption(int v, int min, int max, OnChange = nullptr);

		UciOption& operator=(const std::string&);
		void operator<<(const UciOption&);
		operator int() const;
		operator std::string() const;

	private:
		friend std::ostream& operator<<(std::ostream&, const UciOptionsMap&);

		int min, max;
		int index;
		std::string defaultWaarde, waarde, type;
		OnChange on_change;
	};

	void initialize(UciOptionsMap&);
	void mainLoop(int argc, char* argv[]);
	std::string waarde(Value v);
	std::string veld(Square veld);
	std::string zet(Move zet, const Position& pos);
	std::string pv(const Position& pos, Value alfa, Value beta, int actievePV, int actieveZet);
	Move move_from_string(const Position& pos, std::string& str);
	int strength_for_elo(int elo);
}

extern UCI::UciOptionsMap UciOpties;
extern int TUNE_1, TUNE_2, TUNE_3, TUNE_4, TUNE_5, TUNE_6, TUNE_7, TUNE_8, TUNE_9, TUNE_10;
extern int TUNE_11, TUNE_12, TUNE_13, TUNE_14, TUNE_15, TUNE_16, TUNE_17, TUNE_18, TUNE_19, TUNE_20;

#endif