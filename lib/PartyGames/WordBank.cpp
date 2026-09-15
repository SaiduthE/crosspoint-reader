#include "WordBank.h"

namespace party {
namespace words {

namespace {

// Codenames tiles. Short enough to fit a tile at the UI font size, uppercase so
// the grid reads at arm's length across a table.
constexpr char kCodeWords[][CODE_WORD_STRIDE] = {
    "APPLE",     "ANCHOR",    "ANGEL",    "ARCTIC",    "ATLANTIS", "BADGE",     "BAGEL",   "BALL",      "BAND",
    "BANK",      "BAR",       "BARK",     "BEACH",     "BEAM",     "BEAR",      "BEAT",    "BED",       "BEIJING",
    "BELL",      "BELT",      "BERLIN",   "BERMUDA",   "BERRY",    "BILL",      "BLOCK",   "BOARD",     "BOLT",
    "BOMB",      "BOND",      "BOOM",     "BOOT",      "BOTTLE",   "BOW",       "BOX",     "BRIDGE",    "BRUSH",
    "BUCK",      "BUFFALO",   "BUG",      "BUGLE",     "BUTTON",   "CALF",      "CANADA",  "CAP",       "CAPITAL",
    "CAR",       "CARD",      "CARROT",   "CASINO",    "CAST",     "CAT",       "CELL",    "CENTAUR",   "CENTER",
    "CHAIR",     "CHANGE",    "CHARGE",   "CHECK",     "CHEST",    "CHICK",     "CHINA",   "CHOCOLATE", "CHURCH",
    "CIRCLE",    "CLIFF",     "CLOAK",    "CLUB",      "CODE",     "COLD",      "COMIC",   "COMPOUND",  "CONCERT",
    "CONDUCTOR", "CONTRACT",  "COOK",     "COPPER",    "COTTON",   "COURT",     "COVER",   "CRANE",     "CRASH",
    "CRICKET",   "CROSS",     "CROWN",    "CYCLE",     "CZECH",    "DANCE",     "DATE",    "DAY",       "DEATH",
    "DECK",      "DEGREE",    "DIAMOND",  "DICE",      "DINOSAUR", "DISEASE",   "DOCTOR",  "DOG",       "DRAFT",
    "DRAGON",    "DRESS",     "DRILL",    "DROP",      "DUCK",     "DWARF",     "EAGLE",   "EGYPT",     "EMBASSY",
    "ENGINE",    "ENGLAND",   "EUROPE",   "EYE",       "FACE",     "FAIR",      "FALL",    "FAN",       "FENCE",
    "FIELD",     "FIGHTER",   "FIGURE",   "FILE",      "FILM",     "FIRE",      "FISH",    "FLUTE",     "FLY",
    "FOOT",      "FORCE",     "FOREST",   "FORK",      "FRANCE",   "GAME",      "GAS",     "GENIUS",    "GERMANY",
    "GHOST",     "GIANT",     "GLASS",    "GLOVE",     "GOLD",     "GRACE",     "GRASS",   "GREECE",    "GREEN",
    "GROUND",    "HAM",       "HAND",     "HAWK",      "HEAD",     "HEART",     "HONEY",   "HOOD",      "HOOK",
    "HORN",      "HORSE",     "HOSPITAL", "HOTEL",     "ICE",      "INDIA",     "IRON",    "IVORY",     "JACK",
    "JAM",       "JET",       "JUPITER",  "KANGAROO",  "KETCHUP",  "KEY",       "KID",     "KING",      "KIWI",
    "KNIFE",     "KNIGHT",    "LAB",      "LAP",       "LASER",    "LAWYER",    "LEAD",    "LEMON",     "LIFE",
    "LIGHT",     "LIMOUSINE", "LINE",     "LINK",      "LION",     "LITTER",    "LOCH",    "LOCK",      "LOG",
    "LONDON",    "LUCK",      "MAIL",     "MAMMOTH",   "MAPLE",    "MARBLE",    "MARCH",   "MASS",      "MATCH",
    "MERCURY",   "MEXICO",    "MINE",     "MINT",      "MISSILE",  "MODEL",     "MOLE",    "MOON",      "MOSCOW",
    "MOUNT",     "MOUSE",     "MOUTH",    "MUG",       "NAIL",     "NEEDLE",    "NET",     "NEW",       "NIGHT",
    "NINJA",     "NOTE",      "NOVEL",    "NURSE",     "NUT",      "OCTOPUS",   "OIL",     "OLIVE",     "OLYMPUS",
    "OPERA",     "ORANGE",    "ORGAN",    "PALM",      "PAN",      "PANTS",     "PAPER",   "PARACHUTE", "PARK",
    "PART",      "PASS",      "PASTE",    "PENGUIN",   "PHOENIX",  "PIANO",     "PIE",     "PILOT",     "PIN",
    "PIPE",      "PIRATE",    "PISTOL",   "PIT",       "PITCH",    "PLANE",     "PLASTIC", "PLATE",     "PLATYPUS",
    "PLAY",      "PLOT",      "POINT",    "POISON",    "POLE",     "POLICE",    "POOL",    "PORT",      "POST",
    "POUND",     "PRESS",     "PRINCESS", "PUMPKIN",   "PUPIL",    "PYRAMID",   "QUEEN",   "RABBIT",    "RACKET",
    "RAY",       "RING",      "ROBIN",    "ROBOT",     "ROCK",     "ROME",      "ROOT",    "ROSE",      "ROULETTE",
    "ROUND",     "ROW",       "RULER",    "SATELLITE", "SATURN",   "SCALE",     "SCHOOL",  "SCIENTIST", "SCORPION",
    "SCREEN",    "SCUBA",     "SEAL",     "SERVER",    "SHADOW",   "SHARK",     "SHIP",    "SHOE",      "SHOP",
    "SHOT",      "SINK",      "SLIP",     "SLUG",      "SMUGGLER", "SNOW",      "SNOWMAN", "SOCK",      "SOLDIER",
    "SOUL",      "SOUND",     "SPACE",    "SPELL",     "SPIDER",   "SPIKE",     "SPINE",   "SPOT",      "SPRING",
    "SPY",       "SQUARE",    "STADIUM",  "STAFF",     "STAR",     "STATE",     "STICK",   "STOCK",     "STRAW",
    "STREAM",    "STRIKE",    "STRING",   "SUB",       "SUIT",     "SUPERHERO", "SWING",   "SWITCH",    "TABLE",
    "TABLET",    "TAG",       "TAIL",     "TAP",       "TEACHER",  "TELESCOPE", "TEMPLE",  "THEATER",   "THIEF",
    "THUMB",     "TICK",      "TIE",      "TIME",      "TOKYO",    "TOOTH",     "TORCH",   "TOWER",     "TRACK",
    "TRAIN",     "TRIANGLE",  "TRIP",     "TRUNK",     "TUBE",     "TURKEY",    "UNICORN", "VACUUM",    "VAN",
    "VET",       "WAKE",      "WALL",     "WAR",       "WASHER",   "WATCH",     "WATER",   "WAVE",      "WEB",
    "WELL",      "WHALE",     "WHIP",     "WIND",      "WITCH",    "WORM",      "YARD",
};

// Undercover word pairs: the civilians share the first word, the undercover gets
// the second. Close enough that a careless clue gives the civilian away.
constexpr char kPairs[][2][PAIR_WORD_STRIDE] = {
    {"Coffee", "Tea"},      {"Ocean", "Lake"},        {"Guitar", "Violin"},  {"Winter", "Autumn"},
    {"Pizza", "Burger"},    {"Doctor", "Nurse"},      {"Train", "Bus"},      {"Candle", "Lamp"},
    {"Butter", "Cheese"},   {"Pencil", "Pen"},        {"Rabbit", "Hare"},    {"Castle", "Palace"},
    {"River", "Canal"},     {"Sugar", "Honey"},       {"Jacket", "Coat"},    {"Cinema", "Theatre"},
    {"Soup", "Stew"},       {"Bicycle", "Scooter"},   {"Mirror", "Window"},  {"Pillow", "Cushion"},
    {"Whisper", "Mumble"},  {"Island", "Peninsula"},  {"Chess", "Checkers"}, {"Cloud", "Fog"},
    {"Comet", "Meteor"},    {"Hotel", "Hostel"},      {"Novel", "Diary"},    {"Statue", "Sculpture"},
    {"Desert", "Savanna"},  {"Piano", "Organ"},       {"Spoon", "Ladle"},    {"Beard", "Moustache"},
    {"Dolphin", "Whale"},   {"Crown", "Tiara"},       {"Ladder", "Stairs"},  {"Blanket", "Duvet"},
    {"Magnet", "Battery"},  {"Puzzle", "Riddle"},     {"Yogurt", "Pudding"}, {"Lawyer", "Judge"},
    {"Harbour", "Airport"}, {"Thunder", "Lightning"}, {"Sofa", "Bench"},     {"Wallet", "Purse"},
    {"Rocket", "Missile"},  {"Bakery", "Cafe"},       {"Tunnel", "Bridge"},  {"Perfume", "Incense"},
    {"Marathon", "Sprint"}, {"Compass", "Map"},
};

}  // namespace

uint16_t codeWordCount() { return sizeof(kCodeWords) / sizeof(kCodeWords[0]); }

const char* codeWord(const uint16_t index) { return kCodeWords[index % codeWordCount()]; }

uint16_t pairCount() { return sizeof(kPairs) / sizeof(kPairs[0]); }

const char* pairCivilian(const uint16_t index) { return kPairs[index % pairCount()][0]; }

const char* pairUndercover(const uint16_t index) { return kPairs[index % pairCount()][1]; }

}  // namespace words
}  // namespace party
