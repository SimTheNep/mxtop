CXX := g++

CXXFLAGS := -std=c++17 -I. -I/usr/local/include -Imidi_reader/craigsapp

RTMIDI_CFLAGS := $(shell pkg-config --cflags rtmidi 2>/dev/null)
RTMIDI_LIBS   := $(shell pkg-config --libs rtmidi 2>/dev/null || echo "-lrtmidi")
FTXUI_LIBS    := -L/usr/local/lib -lftxui-component -lftxui-dom -lftxui-screen

CXXFLAGS += $(RTMIDI_CFLAGS)

CRAIGAPP_SRCS := $(wildcard midi_reader/craigsapp/*.cpp)
CRAIGAPP_OBJS := $(CRAIGAPP_SRCS:.cpp=.o)

COMMON_SRCS := \
    json_parser/parser_layouts.cpp \
    json_parser/parser_module.cpp \
    json_parser/parser_dictionary.cpp \
    json_parser/parser_debug.cpp \
    state_layer/state_const.cpp \
    state_layer/state_query.cpp \
    state_layer/state_sysex.cpp \
    state_layer/state_main.cpp \
    state_layer/state_events.cpp \
    midi_reader/queue.cpp \
    midi_reader/midi_load.cpp \
    $(CRAIGAPP_SRCS)

COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)

# Automatically locates settings.cpp in config/, tui/, or root folder
SETTINGS_SRC := $(wildcard config/settings.cpp tui/settings.cpp settings.cpp)

MAIN_SRCS := \
    main.cpp \
    $(SETTINGS_SRC) \
    tui/ui_model.cpp \
    tui/layouts.cpp \
    tui/ui_main.cpp \
    tui/widgets/system_resources.cpp \
    tui/widgets/status_log.cpp \
    tui/widgets/activity_log.cpp \
    tui/debug/debug_ui.cpp \
    tui/menu/menu_helpers.cpp \
    tui/menu/menu_ui.cpp \
    tui/menu/menu_visuals.cpp \
    tui/menu/menu_queue.cpp \
    tui/menu/menu_settings.cpp \
    tui/menu/menu_help.cpp

MAIN_OBJS := $(MAIN_SRCS:.cpp=.o)

STATE_SRCS := \
    state_layer/state_test.cpp

STATE_OBJS := $(STATE_SRCS:.cpp=.o)

JSON_SRCS := \
    json_parser/scratch.cpp \
    json_parser/parser_layouts.cpp \
    json_parser/parser_module.cpp \
    json_parser/parser_dictionary.cpp \
    json_parser/parser_debug.cpp

JSON_OBJS := $(JSON_SRCS:.cpp=.o)

MAIN_TARGET := maintest
MIDI_TARGET := midi_reader/miditest
STATE_TARGET := state_layer/statetest
JSON_TARGET := json_parser/jsontest

ALL_TARGETS := \
    $(MAIN_TARGET) \
    $(MIDI_TARGET) \
    $(STATE_TARGET) \
    $(JSON_TARGET)

all: $(ALL_TARGETS) cleanup-objects

%.o: %.cpp
	@printf "[CXX] %s\n" "$<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(MAIN_TARGET): $(MAIN_OBJS) $(COMMON_OBJS)
	@printf "[LD ] %s\n" "$@"
	@$(CXX) $^ $(RTMIDI_LIBS) $(FTXUI_LIBS) -lncurses -o $@

$(MIDI_TARGET): $(MAIN_OBJS) $(COMMON_OBJS)
	@printf "[LD ] %s\n" "$@"
	@mkdir -p midi_reader
	@$(CXX) $^ $(RTMIDI_LIBS) $(FTXUI_LIBS) -lncurses -o $@

$(STATE_TARGET): $(STATE_OBJS) $(COMMON_OBJS)
	@printf "[LD ] %s\n" "$@"
	@mkdir -p state_layer
	@$(CXX) $^ -o $@

$(JSON_TARGET): $(JSON_OBJS)
	@printf "[LD ] %s\n" "$@"
	@mkdir -p json_parser
	@$(CXX) $^ -o $@

# cleanup-objects:
# 	@printf "[RM ] object files\n"
# 	@find . -name "*.o" -delete

# clean:
# 	@printf "[RM ] object files\n"
# 	@find . -name "*.o" -delete
# 	@printf "[RM ] executables\n"
# 	@rm -f $(ALL_TARGETS)

.PHONY: all clean