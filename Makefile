CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc -Ithird_party/imgui -Ithird_party/imgui/backends
LDFLAGS := -lGL -lglut

TARGET := sfxDesigner
SRC := \
	src/main.cpp \
	src/sfx_def.cpp \
	src/synth_engine.cpp \
	src/wav_writer.cpp \
	src/json_exporter.cpp \
	third_party/imgui/imgui.cpp \
	third_party/imgui/imgui_draw.cpp \
	third_party/imgui/imgui_tables.cpp \
	third_party/imgui/imgui_widgets.cpp \
	third_party/imgui/backends/imgui_impl_glut.cpp \
	third_party/imgui/backends/imgui_impl_opengl2.cpp

OBJ := $(SRC:.cpp=.o)

.PHONY: all clean setup-imgui

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

setup-imgui:
	mkdir -p third_party
	if [ ! -d third_party/imgui/.git ]; then git clone https://github.com/ocornut/imgui.git third_party/imgui; else echo "imgui already present"; fi
