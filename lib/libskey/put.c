/* OpenBSD S/Key (put.c)
 *
 * Authors:
 *          Neil M. Haller <nmh@thumper.bellcore.com>
 *          Philip R. Karn <karn@chicago.qualcomm.com>
 *          John S. Walden <jsw@thumper.bellcore.com>
 *          Scott Chasin <chasin@crimelab.com>
 *
 * Dictionary lookup and extraction.
 *
 * $OpenBSD: put.c,v 1.14 2013/11/29 19:00:51 deraadt Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "skey.h"

static unsigned int extract(char *, int, int);
static void standard(char *);
static void insert(char *, int, int, int);
static int wsrch(char *, int, int);

/* Standard dictionary for integer-word translations */
static const char * const Wp[2048] = {
	"A",      "ABE",    "ACE",    "ACT",    "AD",     "ADA",    "ADD",
	"AGO",    "AID",    "AIM",    "AIR",    "ALL",    "ALP",    "AM",
	"AMY",    "AN",     "ANA",    "AND",    "ANN",    "ANT",    "ANY",
	"APE",    "APS",    "APT",    "ARC",    "ARE",    "ARK",    "ARM",
	"ART",    "AS",     "ASH",    "ASK",    "AT",     "ATE",    "AUG",
	"AUK",    "AVE",    "AWE",    "AWK",    "AWL",    "AWN",    "AX",
	"AYE",    "BAD",    "BAG",    "BAH",    "BAM",    "BAN",    "BAR",
	"BAT",    "BAY",    "BE",     "BED",    "BEE",    "BEG",    "BEN",
	"BET",    "BEY",    "BIB",    "BID",    "BIG",    "BIN",    "BIT",
	"BOB",    "BOG",    "BON",    "BOO",    "BOP",    "BOW",    "BOY",
	"BUB",    "BUD",    "BUG",    "BUM",    "BUN",    "BUS",    "BUT",
	"BUY",    "BY",     "BYE",    "CAB",    "CAL",    "CAM",    "CAN",
	"CAP",    "CAR",    "CAT",    "CAW",    "COD",    "COG",    "COL",
	"CON",    "COO",    "COP",    "COT",    "COW",    "COY",    "CRY",
	"CUB",    "CUE",    "CUP",    "CUR",    "CUT",    "DAB",    "DAD",
	"DAM",    "DAN",    "DAR",    "DAY",    "DEE",    "DEL",    "DEN",
	"DES",    "DEW",    "DID",    "DIE",    "DIG",    "DIN",    "DIP",
	"DO",     "DOE",    "DOG",    "DON",    "DOT",    "DOW",    "DRY",
	"DUB",    "DUD",    "DUE",    "DUG",    "DUN",    "EAR",    "EAT",
	"ED",     "EEL",    "EGG",    "EGO",    "ELI",    "ELK",    "ELM",
	"ELY",    "EM",     "END",    "EST",    "ETC",    "EVA",    "EVE",
	"EWE",    "EYE",    "FAD",    "FAN",    "FAR",    "FAT",    "FAY",
	"FED",    "FEE",    "FEW",    "FIB",    "FIG",    "FIN",    "FIR",
	"FIT",    "FLO",    "FLY",    "FOE",    "FOG",    "FOR",    "FRY",
	"FUM",    "FUN",    "FUR",    "GAB",    "GAD",    "GAG",    "GAL",
	"GAM",    "GAP",    "GAS",    "GAY",    "GEE",    "GEL",    "GEM",
	"GET",    "GIG",    "GIL",    "GIN",    "GO",     "GOT",    "GUM",
	"GUN",    "GUS",    "GUT",    "GUY",    "GYM",    "GYP",    "HA",
	"HAD",    "HAL",    "HAM",    "HAN",    "HAP",    "HAS",    "HAT",
	"HAW",    "HAY",    "HE",     "HEM",    "HEN",    "HER",    "HEW",
	"HEY",    "HI",     "HID",    "HIM",    "HIP",    "HIS",    "HIT",
	"HO",     "HOB",    "HOC",    "HOE",    "HOG",    "HOP",    "HOT",
	"HOW",    "HUB",    "HUE",    "HUG",    "HUH",    "HUM",    "HUT",
	"I",      "ICY",    "IDA",    "IF",     "IKE",    "ILL",    "INK",
	"INN",    "IO",     "ION",    "IQ",     "IRA",    "IRE",    "IRK",
	"IS",     "IT",     "ITS",    "IVY",    "JAB",    "JAG",    "JAM",
	"JAN",    "JAR",    "JAW",    "JAY",    "JET",    "JIG",    "JIM",
	"JO",     "JOB",    "JOE",    "JOG",    "JOT",    "JOY",    "JUG",
	"JUT",    "KAY",    "KEG",    "KEN",    "KEY",    "KID",    "KIM",
	"KIN",    "KIT",    "LA",     "LAB",    "LAC",    "LAD",    "LAG",
	"LAM",    "LAP",    "LAW",    "LAY",    "LEA",    "LED",    "LEE",
	"LEG",    "LEN",    "LEO",    "LET",    "LEW",    "LID",    "LIE",
	"LIN",    "LIP",    "LIT",    "LO",     "LOB",    "LOG",    "LOP",
	"LOS",    "LOT",    "LOU",    "LOW",    "LOY",    "LUG",    "LYE",
	"MA",     "MAC",    "MAD",    "MAE",    "MAN",    "MAO",    "MAP",
	"MAT",    "MAW",    "MAY",    "ME",     "MEG",    "MEL",    "MEN",
	"MET",    "MEW",    "MID",    "MIN",    "MIT",    "MOB",    "MOD",
	"MOE",    "MOO",    "MOP",    "MOS",    "MOT",    "MOW",    "MUD",
	"MUG",    "MUM",    "MY",     "NAB",    "NAG",    "NAN",    "NAP",
	"NAT",    "NAY",    "NE",     "NED",    "NEE",    "NET",    "NEW",
	"NIB",    "NIL",    "NIP",    "NIT",    "NO",     "NOB",    "NOD",
	"NON",    "NOR",    "NOT",    "NOV",    "NOW",    "NU",     "NUN",
	"NUT",    "O",      "OAF",    "OAK",    "OAR",    "OAT",    "ODD",
	"ODE",    "OF",     "OFF",    "OFT",    "OH",     "OIL",    "OK",
	"OLD",    "ON",     "ONE",    "OR",     "ORB",    "ORE",    "ORR",
	"OS",     "OTT",    "OUR",    "OUT",    "OVA",    "OW",     "OWE",
	"OWL",    "OWN",    "OX",     "PA",     "PAD",    "PAL",    "PAM",
	"PAN",    "PAP",    "PAR",    "PAT",    "PAW",    "PAY",    "PEA",
	"PEG",    "PEN",    "PEP",    "PER",    "PET",    "PEW",    "PHI",
	"PI",     "PIE",    "PIN",    "PIT",    "PLY",    "PO",     "POD",
	"POE",    "POP",    "POT",    "POW",    "PRO",    "PRY",    "PUB",
	"PUG",    "PUN",    "PUP",    "PUT",    "QUO",    "RAG",    "RAM",
	"RAN",    "RAP",    "RAT",    "RAW",    "RAY",    "REB",    "RED",
	"REP",    "RET",    "RIB",    "RID",    "RIG",    "RIM",    "RIO",
	"RIP",    "ROB",    "ROD",    "ROE",    "RON",    "ROT",    "ROW",
	"ROY",    "RUB",    "RUE",    "RUG",    "RUM",    "RUN",    "RYE",
	"SAC",    "SAD",    "SAG",    "SAL",    "SAM",    "SAN",    "SAP",
	"SAT",    "SAW",    "SAY",    "SEA",    "SEC",    "SEE",    "SEN",
	"SET",    "SEW",    "SHE",    "SHY",    "SIN",    "SIP",    "SIR",
	"SIS",    "SIT",    "SKI",    "SKY",    "SLY",    "SO",     "SOB",
	"SOD",    "SON",    "SOP",    "SOW",    "SOY",    "SPA",    "SPY",
	"SUB",    "SUD",    "SUE",    "SUM",    "SUN",    "SUP",    "TAB",
	"TAD",    "TAG",    "TAN",    "TAP",    "TAR",    "TEA",    "TED",
	"TEE",    "TEN",    "THE",    "THY",    "TIC",    "TIE",    "TIM",
	"TIN",    "TIP",    "TO",     "TOE",    "TOG",    "TOM",    "TON",
	"TOO",    "TOP",    "TOW",    "TOY",    "TRY",    "TUB",    "TUG",
	"TUM",    "TUN",    "TWO",    "UN",     "UP",     "US",     "USE",
	"VAN",    "VAT",    "VET",    "VIE",    "WAD",    "WAG",    "WAR",
	"WAS",    "WAY",    "WE",     "WEB",    "WED",    "WEE",    "WET",
	"WHO",    "WHY",    "WIN",    "WIT",    "WOK",    "WON",    "WOO",
	"WOW",    "WRY",    "WU",     "YAM",    "YAP",    "YAW",    "YE",
	"YEA",    "YES",    "YET",    "YOU",    "ABED",   "ABEL",   "ABET",
	"ABLE",   "ABUT",   "ACHE",   "ACID",   "ACME",   "ACRE",   "ACTA",
	"ACTS",   "ADAM",   "ADDS",   "ADEN",   "AFAR",   "AFRO",   "AGEE",
	"AHEM",   "AHOY",   "AIDA",   "AIDE",   "AIDS",   "AIRY",   "AJAR",
	"AKIN",   "ALAN",   "ALEC",   "ALGA",   "ALIA",   "ALLY",   "ALMA",
	"ALOE",   "ALSO",   "ALTO",   "ALUM",   "ALVA",   "AMEN",   "AMES",
	"AMID",   "AMMO",   "AMOK",   "AMOS",   "AMRA",   "ANDY",   "ANEW",
	"ANNA",   "ANNE",   "ANTE",   "ANTI",   "AQUA",   "ARAB",   "ARCH",
	"AREA",   "ARGO",   "ARID",   "ARMY",   "ARTS",   "ARTY",   "ASIA",
	"ASKS",   "ATOM",   "AUNT",   "AURA",   "AUTO",   "AVER",   "AVID",
	"AVIS",   "AVON",   "AVOW",   "AWAY",   "AWRY",   "BABE",   "BABY",
	"BACH",   "BACK",   "BADE",   "BAIL",   "BAIT",   "BAKE",   "BALD",
	"BALE",   "BALI",   "BALK",   "BALL",   "BALM",   "BAND",   "BANE",
	"BANG",   "BANK",   "BARB",   "BARD",   "BARE",   "BARK",   "BARN",
	"BARR",   "BASE",   "BASH",   "BASK",   "BASS",   "BATE",   "BATH",
	"BAWD",   "BAWL",   "BEAD",   "BEAK",   "BEAM",   "BEAN",   "BEAR",
	"BEAT",   "BEAU",   "BECK",   "BEEF",   "BEEN",   "BEER",   "BEET",
	"BELA",   "BELL",   "BELT",   "BEND",   "BENT",   "BERG",   "BERN",
	"BERT",   "BESS",   "BEST",   "BETA",   "BETH",   "BHOY",   "BIAS",
	"BIDE",   "BIEN",   "BILE",   "BILK",   "BILL",   "BIND",   "BING",
	"BIRD",   "BITE",   "BITS",   "BLAB",   "BLAT",   "BLED",   "BLEW",
	"BLOB",   "BLOC",   "BLOT",   "BLOW",   "BLUE",   "BLUM",   "BLUR",
	"BOAR",   "BOAT",   "BOCA",   "BOCK",   "BODE",   "BODY",   "BOGY",
	"BOHR",   "BOIL",   "BOLD",   "BOLO",   "BOLT",   "BOMB",   "BONA",
	"BOND",   "BONE",   "BONG",   "BONN",   "BONY",   "BOOK",   "BOOM",
	"BOON",   "BOOT",   "BORE",   "BORG",   "BORN",   "BOSE",   "BOSS",
	"BOTH",   "BOUT",   "BOWL",   "BOYD",   "BRAD",   "BRAE",   "BRAG",
	"BRAN",   "BRAY",   "BRED",   "BREW",   "BRIG",   "BRIM",   "BROW",
	"BUCK",   "BUDD",   "BUFF",   "BULB",   "BULK",   "BULL",   "BUNK",
	"BUNT",   "BUOY",   "BURG",   "BURL",   "BURN",   "BURR",   "BURT",
	"BURY",   "BUSH",   "BUSS",   "BUST",   "BUSY",   "BYTE",   "CADY",
	"CAFE",   "CAGE",   "CAIN",   "CAKE",   "CALF",   "CALL",   "CALM",
	"CAME",   "CANE",   "CANT",   "CARD",   "CARE",   "CARL",   "CARR",
	"CART",   "CASE",   "CASH",   "CASK",   "CAST",   "CAVE",   "CEIL",
	"CELL",   "CENT",   "CERN",   "CHAD",   "CHAR",   "CHAT",   "CHAW",
	"CHEF",   "CHEN",   "CHEW",   "CHIC",   "CHIN",   "CHOU",   "CHOW",
	"CHUB",   "CHUG",   "CHUM",   "CITE",   "CITY",   "CLAD",   "CLAM",
	"CLAN",   "CLAW",   "CLAY",   "CLOD",   "CLOG",   "CLOT",   "CLUB",
	"CLUE",   "COAL",   "COAT",   "COCA",   "COCK",   "COCO",   "CODA",
	"CODE",   "CODY",   "COED",   "COIL",   "COIN",   "COKE",   "COLA",
	"COLD",   "COLT",   "COMA",   "COMB",   "COME",   "COOK",   "COOL",
	"COON",   "COOT",   "CORD",   "CORE",   "CORK",   "CORN",   "COST",
	"COVE",   "COWL",   "CRAB",   "CRAG",   "CRAM",   "CRAY",   "CREW",
	"CRIB",   "CROW",   "CRUD",   "CUBA",   "CUBE",   "CUFF",   "CULL",
	"CULT",   "CUNY",   "CURB",   "CURD",   "CURE",   "CURL",   "CURT",
	"CUTS",   "DADE",   "DALE",   "DAME",   "DANA",   "DANE",   "DANG",
	"DANK",   "DARE",   "DARK",   "DARN",   "DART",   "DASH",   "DATA",
	"DATE",   "DAVE",   "DAVY",   "DAWN",   "DAYS",   "DEAD",   "DEAF",
	"DEAL",   "DEAN",   "DEAR",   "DEBT",   "DECK",   "DEED",   "DEEM",
	"DEER",   "DEFT",   "DEFY",   "DELL",   "DENT",   "DENY",   "DESK",
	"DIAL",   "DICE",   "DIED",   "DIET",   "DIME",   "DINE",   "DING",
	"DINT",   "DIRE",   "DIRT",   "DISC",   "DISH",   "DISK",   "DIVE",
	"DOCK",   "DOES",   "DOLE",   "DOLL",   "DOLT",   "DOME",   "DONE",
	"DOOM",   "DOOR",   "DORA",   "DOSE",   "DOTE",   "DOUG",   "DOUR",
	"DOVE",   "DOWN",   "DRAB",   "DRAG",   "DRAM",   "DRAW",   "DREW",
	"DRUB",   "DRUG",   "DRUM",   "DUAL",   "DUCK",   "DUCT",   "DUEL",
	"DUET",   "DUKE",   "DULL",   "DUMB",   "DUNE",   "DUNK",   "DUSK",
	"DUST",   "DUTY",   "EACH",   "EARL",   "EARN",   "EASE",   "EAST",
	"EASY",   "EBEN",   "ECHO",   "EDDY",   "EDEN",   "EDGE",   "EDGY",
	"EDIT",   "EDNA",   "EGAN",   "ELAN",   "ELBA",   "ELLA",   "ELSE",
	"EMIL",   "EMIT",   "EMMA",   "ENDS",   "ERIC",   "EROS",   "EVEN",
	"EVER",   "EVIL",   "EYED",   "FACE",   "FACT",   "FADE",   "FAIL",
	"FAIN",   "FAIR",   "FAKE",   "FALL",   "FAME",   "FANG",   "FARM",
	"FAST",   "FATE",   "FAWN",   "FEAR",   "FEAT",   "FEED",   "FEEL",
	"FEET",   "FELL",   "FELT",   "FEND",   "FERN",   "FEST",   "FEUD",
	"FIEF",   "FIGS",   "FILE",   "FILL",   "FILM",   "FIND",   "FINE",
	"FINK",   "FIRE",   "FIRM",   "FISH",   "FISK",   "FIST",   "FITS",
	"FIVE",   "FLAG",   "FLAK",   "FLAM",   "FLAT",   "FLAW",   "FLEA",
	"FLED",   "FLEW",   "FLIT",   "FLOC",   "FLOG",   "FLOW",   "FLUB",
	"FLUE",   "FOAL",   "FOAM",   "FOGY",   "FOIL",   "FOLD",   "FOLK",
	"FOND",   "FONT",   "FOOD",   "FOOL",   "FOOT",   "FORD",   "FORE",
	"FORK",   "FORM",   "FORT",   "FOSS",   "FOUL",   "FOUR",   "FOWL",
	"FRAU",   "FRAY",   "FRED",   "FREE",   "FRET",   "FREY",   "FROG",
	"FROM",   "FUEL",   "FULL",   "FUME",   "FUND",   "FUNK",   "FURY",
	"FUSE",   "FUSS",   "GAFF",   "GAGE",   "GAIL",   "GAIN",   "GAIT",
	"GALA",   "GALE",   "GALL",   "GALT",   "GAME",   "GANG",   "GARB",
	"GARY",   "GASH",   "GATE",   "GAUL",   "GAUR",   "GAVE",   "GAWK",
	"GEAR",   "GELD",   "GENE",   "GENT",   "GERM",   "GETS",   "GIBE",
	"GIFT",   "GILD",   "GILL",   "GILT",   "GINA",   "GIRD",   "GIRL",
	"GIST",   "GIVE",   "GLAD",   "GLEE",   "GLEN",   "GLIB",   "GLOB",
	"GLOM",   "GLOW",   "GLUE",   "GLUM",   "GLUT",   "GOAD",   "GOAL",
	"GOAT",   "GOER",   "GOES",   "GOLD",   "GOLF",   "GONE",   "GONG",
	"GOOD",   "GOOF",   "GORE",   "GORY",   "GOSH",   "GOUT",   "GOWN",
	"GRAB",   "GRAD",   "GRAY",   "GREG",   "GREW",   "GREY",   "GRID",
	"GRIM",   "GRIN",   "GRIT",   "GROW",   "GRUB",   "GULF",   "GULL",
	"GUNK",   "GURU",   "GUSH",   "GUST",   "GWEN",   "GWYN",   "HAAG",
	"HAAS",   "HACK",   "HAIL",   "HAIR",   "HALE",   "HALF",   "HALL",
	"HALO",   "HALT",   "HAND",   "HANG",   "HANK",   "HANS",   "HARD",
	"HARK",   "HARM",   "HART",   "HASH",   "HAST",   "HATE",   "HATH",
	"HAUL",   "HAVE",   "HAWK",   "HAYS",   "HEAD",   "HEAL",   "HEAR",
	"HEAT",   "HEBE",   "HECK",   "HEED",   "HEEL",   "HEFT",   "HELD",
	"HELL",   "HELM",   "HERB",   "HERD",   "HERE",   "HERO",   "HERS",
	"HESS",   "HEWN",   "HICK",   "HIDE",   "HIGH",   "HIKE",   "HILL",
	"HILT",   "HIND",   "HINT",   "HIRE",   "HISS",   "HIVE",   "HOBO",
	"HOCK",   "HOFF",   "HOLD",   "HOLE",   "HOLM",   "HOLT",   "HOME",
	"HONE",   "HONK",   "HOOD",   "HOOF",   "HOOK",   "HOOT",   "HORN",
	"HOSE",   "HOST",   "HOUR",   "HOVE",   "HOWE",   "HOWL",   "HOYT",
	"HUCK",   "HUED",   "HUFF",   "HUGE",   "HUGH",   "HUGO",   "HULK",
	"HULL",   "HUNK",   "HUNT",   "HURD",   "HURL",   "HURT",   "HUSH",
	"HYDE",   "HYMN",   "IBIS",   "ICON",   "IDEA",   "IDLE",   "IFFY",
	"INCA",   "INCH",   "INTO",   "IONS",   "IOTA",   "IOWA",   "IRIS",
	"IRMA",   "IRON",   "ISLE",   "ITCH",   "ITEM",   "IVAN",   "JACK",
	"JADE",   "JAIL",   "JAKE",   "JANE",   "JAVA",   "JEAN",   "JEFF",
	"JERK",   "JESS",   "JEST",   "JIBE",   "JILL",   "JILT",   "JIVE",
	"JOAN",   "JOBS",   "JOCK",   "JOEL",   "JOEY",   "JOHN",   "JOIN",
	"JOKE",   "JOLT",   "JOVE",   "JUDD",   "JUDE",   "JUDO",   "JUDY",
	"JUJU",   "JUKE",   "JULY",   "JUNE",   "JUNK",   "JUNO",   "JURY",
	"JUST",   "JUTE",   "KAHN",   "KALE",   "KANE",   "KANT",   "KARL",
	"KATE",   "KEEL",   "KEEN",   "KENO",   "KENT",   "KERN",   "KERR",
	"KEYS",   "KICK",   "KILL",   "KIND",   "KING",   "KIRK",   "KISS",
	"KITE",   "KLAN",   "KNEE",   "KNEW",   "KNIT",   "KNOB",   "KNOT",
	"KNOW",   "KOCH",   "KONG",   "KUDO",   "KURD",   "KURT",   "KYLE",
	"LACE",   "LACK",   "LACY",   "LADY",   "LAID",   "LAIN",   "LAIR",
	"LAKE",   "LAMB",   "LAME",   "LAND",   "LANE",   "LANG",   "LARD",
	"LARK",   "LASS",   "LAST",   "LATE",   "LAUD",   "LAVA",   "LAWN",
	"LAWS",   "LAYS",   "LEAD",   "LEAF",   "LEAK",   "LEAN",   "LEAR",
	"LEEK",   "LEER",   "LEFT",   "LEND",   "LENS",   "LENT",   "LEON",
	"LESK",   "LESS",   "LEST",   "LETS",   "LIAR",   "LICE",   "LICK",
	"LIED",   "LIEN",   "LIES",   "LIEU",   "LIFE",   "LIFT",   "LIKE",
	"LILA",   "LILT",   "LILY",   "LIMA",   "LIMB",   "LIME",   "LIND",
	"LINE",   "LINK",   "LINT",   "LION",   "LISA",   "LIST",   "LIVE",
	"LOAD",   "LOAF",   "LOAM",   "LOAN",   "LOCK",   "LOFT",   "LOGE",
	"LOIS",   "LOLA",   "LONE",   "LONG",   "LOOK",   "LOON",   "LOOT",
	"LORD",   "LORE",   "LOSE",   "LOSS",   "LOST",   "LOUD",   "LOVE",
	"LOWE",   "LUCK",   "LUCY",   "LUGE",   "LUKE",   "LULU",   "LUND",
	"LUNG",   "LURA",   "LURE",   "LURK",   "LUSH",   "LUST",   "LYLE",
	"LYNN",   "LYON",   "LYRA",   "MACE",   "MADE",   "MAGI",   "MAID",
	"MAIL",   "MAIN",   "MAKE",   "MALE",   "MALI",   "MALL",   "MALT",
	"MANA",   "MANN",   "MANY",   "MARC",   "MARE",   "MARK",   "MARS",
	"MART",   "MARY",   "MASH",   "MASK",   "MASS",   "MAST",   "MATE",
	"MATH",   "MAUL",   "MAYO",   "MEAD",   "MEAL",   "MEAN",   "MEAT",
	"MEEK",   "MEET",   "MELD",   "MELT",   "MEMO",   "MEND",   "MENU",
	"MERT",   "MESH",   "MESS",   "MICE",   "MIKE",   "MILD",   "MILE",
	"MILK",   "MILL",   "MILT",   "MIMI",   "MIND",   "MINE",   "MINI",
	"MINK",   "MINT",   "MIRE",   "MISS",   "MIST",   "MITE",   "MITT",
	"MOAN",   "MOAT",   "MOCK",   "MODE",   "MOLD",   "MOLE",   "MOLL",
	"MOLT",   "MONA",   "MONK",   "MONT",   "MOOD",   "MOON",   "MOOR",
	"MOOT",   "MORE",   "MORN",   "MORT",   "MOSS",   "MOST",   "MOTH",
	"MOVE",   "MUCH",   "MUCK",   "MUDD",   "MUFF",   "MULE",   "MULL",
	"MURK",   "MUSH",   "MUST",   "MUTE",   "MUTT",   "MYRA",   "MYTH",
	"NAGY",   "NAIL",   "NAIR",   "NAME",   "NARY",   "NASH",   "NAVE",
	"NAVY",   "NEAL",   "NEAR",   "NEAT",   "NECK",   "NEED",   "NEIL",
	"NELL",   "NEON",   "NERO",   "NESS",   "NEST",   "NEWS",   "NEWT",
	"NIBS",   "NICE",   "NICK",   "NILE",   "NINA",   "NINE",   "NOAH",
	"NODE",   "NOEL",   "NOLL",   "NONE",   "NOOK",   "NOON",   "NORM",
	"NOSE",   "NOTE",   "NOUN",   "NOVA",   "NUDE",   "NULL",   "NUMB",
	"OATH",   "OBEY",   "OBOE",   "ODIN",   "OHIO",   "OILY",   "OINT",
	"OKAY",   "OLAF",   "OLDY",   "OLGA",   "OLIN",   "OMAN",   "OMEN",
	"OMIT",   "ONCE",   "ONES",   "ONLY",   "ONTO",   "ONUS",   "ORAL",
	"ORGY",   "OSLO",   "OTIS",   "OTTO",   "OUCH",   "OUST",   "OUTS",
	"OVAL",   "OVEN",   "OVER",   "OWLY",   "OWNS",   "QUAD",   "QUIT",
	"QUOD",   "RACE",   "RACK",   "RACY",   "RAFT",   "RAGE",   "RAID",
	"RAIL",   "RAIN",   "RAKE",   "RANK",   "RANT",   "RARE",   "RASH",
	"RATE",   "RAVE",   "RAYS",   "READ",   "REAL",   "REAM",   "REAR",
	"RECK",   "REED",   "REEF",   "REEK",   "REEL",   "REID",   "REIN",
	"RENA",   "REND",   "RENT",   "REST",   "RICE",   "RICH",   "RICK",
	"RIDE",   "RIFT",   "RILL",   "RIME",   "RING",   "RINK",   "RISE",
	"RISK",   "RITE",   "ROAD",   "ROAM",   "ROAR",   "ROBE",   "ROCK",
	"RODE",   "ROIL",   "ROLL",   "ROME",   "ROOD",   "ROOF",   "ROOK",
	"ROOM",   "ROOT",   "ROSA",   "ROSE",   "ROSS",   "ROSY",   "ROTH",
	"ROUT",   "ROVE",   "ROWE",   "ROWS",   "RUBE",   "RUBY",   "RUDE",
	"RUDY",   "RUIN",   "RULE",   "RUNG",   "RUNS",   "RUNT",   "RUSE",
	"RUSH",   "RUSK",   "RUSS",   "RUST",   "RUTH",   "SACK",   "SAFE",
	"SAGE",   "SAID",   "SAIL",   "SALE",   "SALK",   "SALT",   "SAME",
	"SAND",   "SANE",   "SANG",   "SANK",   "SARA",   "SAUL",   "SAVE",
	"SAYS",   "SCAN",   "SCAR",   "SCAT",   "SCOT",   "SEAL",   "SEAM",
	"SEAR",   "SEAT",   "SEED",   "SEEK",   "SEEM",   "SEEN",   "SEES",
	"SELF",   "SELL",   "SEND",   "SENT",   "SETS",   "SEWN",   "SHAG",
	"SHAM",   "SHAW",   "SHAY",   "SHED",   "SHIM",   "SHIN",   "SHOD",
	"SHOE",   "SHOT",   "SHOW",   "SHUN",   "SHUT",   "SICK",   "SIDE",
	"SIFT",   "SIGH",   "SIGN",   "SILK",   "SILL",   "SILO",   "SILT",
	"SINE",   "SING",   "SINK",   "SIRE",   "SITE",   "SITS",   "SITU",
	"SKAT",   "SKEW",   "SKID",   "SKIM",   "SKIN",   "SKIT",   "SLAB",
	"SLAM",   "SLAT",   "SLAY",   "SLED",   "SLEW",   "SLID",   "SLIM",
	"SLIT",   "SLOB",   "SLOG",   "SLOT",   "SLOW",   "SLUG",   "SLUM",
	"SLUR",   "SMOG",   "SMUG",   "SNAG",   "SNOB",   "SNOW",   "SNUB",
	"SNUG",   "SOAK",   "SOAR",   "SOCK",   "SODA",   "SOFA",   "SOFT",
	"SOIL",   "SOLD",   "SOME",   "SONG",   "SOON",   "SOOT",   "SORE",
	"SORT",   "SOUL",   "SOUR",   "SOWN",   "STAB",   "STAG",   "STAN",
	"STAR",   "STAY",   "STEM",   "STEW",   "STIR",   "STOW",   "STUB",
	"STUN",   "SUCH",   "SUDS",   "SUIT",   "SULK",   "SUMS",   "SUNG",
	"SUNK",   "SURE",   "SURF",   "SWAB",   "SWAG",   "SWAM",   "SWAN",
	"SWAT",   "SWAY",   "SWIM",   "SWUM",   "TACK",   "TACT",   "TAIL",
	"TAKE",   "TALE",   "TALK",   "TALL",   "TANK",   "TASK",   "TATE",
	"TAUT",   "TEAL",   "TEAM",   "TEAR",   "TECH",   "TEEM",   "TEEN",
	"TEET",   "TELL",   "TEND",   "TENT",   "TERM",   "TERN",   "TESS",
	"TEST",   "THAN",   "THAT",   "THEE",   "THEM",   "THEN",   "THEY",
	"THIN",   "THIS",   "THUD",   "THUG",   "TICK",   "TIDE",   "TIDY",
	"TIED",   "TIER",   "TILE",   "TILL",   "TILT",   "TIME",   "TINA",
	"TINE",   "TINT",   "TINY",   "TIRE",   "TOAD",   "TOGO",   "TOIL",
	"TOLD",   "TOLL",   "TONE",   "TONG",   "TONY",   "TOOK",   "TOOL",
	"TOOT",   "TORE",   "TORN",   "TOTE",   "TOUR",   "TOUT",   "TOWN",
	"TRAG",   "TRAM",   "TRAY",   "TREE",   "TREK",   "TRIG",   "TRIM",
	"TRIO",   "TROD",   "TROT",   "TROY",   "TRUE",   "TUBA",   "TUBE",
	"TUCK",   "TUFT",   "TUNA",   "TUNE",   "TUNG",   "TURF",   "TURN",
	"TUSK",   "TWIG",   "TWIN",   "TWIT",   "ULAN",   "UNIT",   "URGE",
	"USED",   "USER",   "USES",   "UTAH",   "VAIL",   "VAIN",   "VALE",
	"VARY",   "VASE",   "VAST",   "VEAL",   "VEDA",   "VEIL",   "VEIN",
	"VEND",   "VENT",   "VERB",   "VERY",   "VETO",   "VICE",   "VIEW",
	"VINE",   "VISE",   "VOID",   "VOLT",   "VOTE",   "WACK",   "WADE",
	"WAGE",   "WAIL",   "WAIT",   "WAKE",   "WALE",   "WALK",   "WALL",
	"WALT",   "WAND",   "WANE",   "WANG",   "WANT",   "WARD",   "WARM",
	"WARN",   "WART",   "WASH",   "WAST",   "WATS",   "WATT",   "WAVE",
	"WAVY",   "WAYS",   "WEAK",   "WEAL",   "WEAN",   "WEAR",   "WEED",
	"WEEK",   "WEIR",   "WELD",   "WELL",   "WELT",   "WENT",   "WERE",
	"WERT",   "WEST",   "WHAM",   "WHAT",   "WHEE",   "WHEN",   "WHET",
	"WHOA",   "WHOM",   "WICK",   "WIFE",   "WILD",   "WILL",   "WIND",
	"WINE",   "WING",   "WINK",   "WINO",   "WIRE",   "WISE",   "WISH",
	"WITH",   "WOLF",   "WONT",   "WOOD",   "WOOL",   "WORD",   "WORE",
	"WORK",   "WORM",   "WORN",   "WOVE",   "WRIT",   "WYNN",   "YALE",
	"YANG",   "YANK",   "YARD",   "YARN",   "YAWL",   "YAWN",   "YEAH",
	"YEAR",   "YELL",   "YOGA",   "YOKE"
};

/*
 * Encode 8 bytes in 'c' as a string of 6 four-letter English words separated
 * by spaces.  The 'out' pointer must have at least 30 bytes for storage.
 */
char *
btoe(char *engout, char *c)
{
	char cp[10];	/* add in room for the parity 2 bits + extract() slop */
	int p, i, indices[6];

	/* workaround for extract() reads beyond end of data */
	(void)memset(cp, 0, sizeof(cp));
	(void)memcpy(cp, c, 8);

	/* compute parity */
	for (p = 0, i = 0; i < 64; i += 2)
		p += extract(cp, i, 2);

	cp[8] = (char)p << 6;

	indices[0] = extract(cp, 0, 11);
	indices[1] = extract(cp, 11, 11);
	indices[2] = extract(cp, 22, 11);
	indices[3] = extract(cp, 33, 11);
	indices[4] = extract(cp, 44, 11);
	indices[5] = extract(cp, 55, 11);

	snprintf(engout, 30, "%.4s %.4s %.4s %.4s %.4s %.4s", Wp[indices[0]],
	    Wp[indices[1]], Wp[indices[2]], Wp[indices[3]],
	    Wp[indices[4]], Wp[indices[5]]);

	return(engout);
}

/*
 * Converts the 6 space-separated english words in 'e' to binary form.
 * The 'out' variable must be at least SKEY_BINKEY_SIZE bytes in size.
 * returns 1 OK - all good words and parity is OK
 *         0 word not in data base
 *        -1 badly formed in put ie > 4 char word
 *        -2 words OK but parity is wrong
 */
int
etob(char *out, char *e)
{
	char *word;
	int i, p, v, l, low, high;
	char b[SKEY_BINKEY_SIZE+1];
	char input[36];
	char *last;

	if (e == NULL)
		return(-1);

	(void)strlcpy(input, e, sizeof(input));
	(void)memset(b, 0, sizeof(b));
	(void)memset(out, 0, SKEY_BINKEY_SIZE);
	for (i = 0, p = 0; i < 6; i++, p += 11) {
		if ((word = strtok_r(i == 0 ? input : NULL, " ", &last)) == NULL)
			return(-1);

		l = strlen(word);
		if (l > 4 || l < 1) {
			return(-1);
		} else if (l < 4) {
			low = 0;
			high = 570;
		} else {
			low = 571;
			high = 2047;
		}
		standard(word);

		if ((v = wsrch(word, low, high)) < 0)
			return(0);

		insert(b, v, p, 11);
	}

	/* now check the parity of what we got */
	for (p = 0, i = 0; i < 64; i += 2)
		p += extract(b, i, 2);

	if ((p & 3) != extract(b, 64, 2))
		return(-2);

	(void)memcpy(out, b, SKEY_BINKEY_SIZE);

	return(1);
}

/*
 * Format 8 bytes as a series of four 16-bit hex digits.
 * The 'out' pointer must have at least 20 bytes for storage.
 */
char *
put8(char *out, char *s)
{
	(void)snprintf(out, 20, "%02X%02X %02X%02X %02X%02X %02X%02X",
			s[0] & 0xff, s[1] & 0xff, s[2] & 0xff,
			s[3] & 0xff, s[4] & 0xff, s[5] & 0xff,
			s[6] & 0xff, s[7] & 0xff);
	return(out);
}

/* Internal subroutines for word encoding/decoding */

/* Dictionary binary search */
static int
wsrch(char *w, int low, int high)
{
	int i, j;

	for (;;) {
		i = (low + high) / 2;

		if ((j = strncmp(w, Wp[i], 4)) == 0)
			return(i);			/* Found it */

		if (high == low + 1) {
			/* Avoid effects of integer truncation in /2 */
			if (strncmp(w, Wp[high], 4) == 0)
				return(high);
			else
				return(-1);
		}

		if (low >= high)
			return(-1);	/* I don't *think* this can happen... */
		if (j < 0)
			high = i;	/* Search lower half */
		else
			low = i;	/* Search upper half */
	}
}

static void
insert(char *s, int x, int start, int length)
{
	unsigned char cl;
	unsigned char cc;
	unsigned char cr;
	unsigned int y;
	int shift;

	assert(length <= 11);
	assert(start >= 0);
	assert(length >= 0);
	assert(start + length <= 66);

	shift = ((8 - ((start + length) % 8)) % 8);
	y = x << shift;
	cl = (y >> 16) & 0xff;
	cc = (y >> 8) & 0xff;
	cr = y & 0xff;
	if (shift + length > 16) {
		s[start / 8] |= cl;
		s[start / 8 + 1] |= cc;
		s[start / 8 + 2] |= cr;
	} else if (shift + length > 8) {
		s[start / 8] |= cc;
		s[start / 8 + 1] |= cr;
	} else {
		s[start / 8] |= cr;
 	}
}

static void
standard(char *word)
{
	while (*word) {
		if (!isascii((unsigned char)*word))
			break;
		if (islower((unsigned char)*word))
			*word = toupper((unsigned char)*word);
		if (*word == '1')
			*word = 'L';
		if (*word == '0')
			*word = 'O';
		if (*word == '5')
			*word = 'S';
		word++;
	}
}

/* Extract 'length' bits from the char array 's' starting with bit 'start' */
static unsigned int
extract(char *s, int start, int length)
{
	unsigned char cl;
	unsigned char cc;
	unsigned char cr;
	unsigned int x;

	assert(length <= 11);
	assert(start >= 0);
	assert(length >= 0);
	assert(start + length <= 66);

	cl = s[start / 8];
	cc = s[start / 8 + 1];
	cr = s[start / 8 + 2];
	x = ((int)(cl << 8 | cc) << 8 | cr);
	x = x >> (24 - (length + (start % 8)));
	x = (x & (0xffff >> (16 - length)));

	return(x);
}
