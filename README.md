Yet another Scouting Tool
===

# About

This app reads player data from your running Football Manager 24 game and lets you search the player database. It also
rates players according to customisable weights.

It has only been tested with
the [Steam version of Football Manager 24](https://store.steampowered.com/app/2252570/Football_Manager_2024/), v24.4.2.

> **Officially supported platforms:** Linux and Windows.
>
> macOS is not an officially supported runtime platform.

## Features

### Search for players

- Search based on a number of factors:
	- name (accents are ignored, so "modric" finds Modrić)
	- age
	- position
	- nationality
	- current/potential ability
	- rating
	- club
	- value
	- home, current and world reputation
	- contract: maximum wage, expiring within a number of months, transfer or loan listed, unattached
	- preferred foot
	- fast learners and hot prospects
	- up to three attributes, hidden and personality attributes included (e.g. professionalism of 15 or more)
- Choose which columns the results show, including any attribute, and in what order, from the **Columns** button. Extra
  columns include preferred foot, weekly wage, contract end date and a value-for-money score

![Screenshot of player search](docs/player-search.png?v=1)

### Analyse players

- See all the attributes for any player (including hidden and personality) as well as a competency rating for all
  comfortable positions
	- These competencies can be customised with either universal or per-position weights
- See a player's wage, contract end date and transfer status
- Click a player's club or flag to search for that club's or nation's players
- Find players with a similar attribute profile, optionally younger or cheaper

### Track players

- Shortlist players from their Player Info window. The shortlist shows how each player's CA, PA, rating, value and
  attributes have changed since you added them, and is saved to **shortlist.tsv** next to the app

![Screenshot of player info](docs/player-info.png?v=1)

### Analyse a squad

- Assemble the best XI from all players in search results, for a given formation, based on player competency ratings
	- Show the bench: the next-best option for every position
	- Find upgrades: the best players outside your search results who would improve each position, within a budget
	  and age limit
- View squad depth, outlining the strength of your options across all positions
	- The squad planner flags positions with no cover, ageing options, expiring contracts or a weak first choice

![Screenshot of best XI](docs/best-xi.png?v=1)
![Screenshot of squad depth](docs/squad-depth.png?v=1)

### Keyboard shortcuts

| Shortcut  | Action                                    |
|-----------|-------------------------------------------|
| Ctrl+F    | Search by name                            |
| Enter     | Open the selected result                  |
| Ctrl+P    | Open the player currently shown in-game   |
| Ctrl+B    | Best XI                                   |
| Ctrl+D    | Squad depth                               |
| Ctrl+L    | Shortlist                                 |
| Ctrl+S    | Shortlist (or un-shortlist) a player, from their Player Info window |
| Ctrl+W    | Close the window                          |

On macOS, use Cmd in place of Ctrl.

## Options

The app can be customised by modifying the **options.yaml** file, which is created on the first run. The options file is
subject to a "hot-reload" meaning changes will be reflected immediately without having to restart the app.

See the [options guide](docs/OPTIONS.md) for more information.

# Feature request?

[Open an issue](https://github.com/PhilipArmstead/Yet-another-Scouting-Tool/issues), clearly labelled as a feature
request, and I'll consider adding it to the app.

# Troubleshooting

For troubleshooting information, see
the [troubleshooting guide](docs/TROUBLESHOOTING.md). If you struggle with an issue you can't
resolve, [open an issue](https://github.com/PhilipArmstead/Yet-another-Scouting-Tool/issues).

# Development

For build requirements, source-build instructions, and developer troubleshooting, see
the [development guide](docs/DEVELOPMENT.md).

# Credits

For attribution of all art assets in this project, see the [credits page](docs/CREDITS.md).

No generative AI was used in the production of any art in this project.

# Support

If you enjoy using this tool and think it's worth a cup of tea, consider kicking me a couple of quid. Any contributions
are **extremely** appreciated.

[![Support me on Ko-fi](docs/kofi.png?v=1)](https://ko-fi.com/philarmstead)