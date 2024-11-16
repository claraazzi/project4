#define STB_IMAGE_IMPLEMENTATION
#define LOG(argument) std::cout << argument << '\n'
#define GL_GLEXT_PROTOTYPES 1
#define FIXED_TIMESTEP 0.0166666f
#define ENEMY_COUNT 3
#define LEVEL1_WIDTH 14
#define LEVEL1_HEIGHT 5
#define FONTBANK_SIZE 16
#ifdef _WINDOWS
#include <GL/glew.h>
#endif

#include <SDL_mixer.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "ShaderProgram.h"
#include "stb_image.h"
#include <cmath>
#include <vector>
#include "Entity.h"
#include "Map.h"

// ————— GAME STATE ————— //
struct GameState {
    Entity *player;
    Entity *enemies;
    Map *map;
    Mix_Music *bgm;
    Mix_Chunk *jump_sfx;
};

enum AppStatus { RUNNING, TERMINATED };

// ————— CONSTANTS ————— //
constexpr int WINDOW_WIDTH = 640 * 2, WINDOW_HEIGHT = 480 * 2;
constexpr float BG_RED = 0.1922f, BG_BLUE = 0.549f, BG_GREEN = 0.9059f, BG_OPACITY = 1.0f;

constexpr int VIEWPORT_X = 0, VIEWPORT_Y = 0, VIEWPORT_WIDTH = WINDOW_WIDTH, VIEWPORT_HEIGHT = WINDOW_HEIGHT;
constexpr char GAME_WINDOW_NAME[] = "Hello, Maps!";
constexpr char V_SHADER_PATH[] = "shaders/vertex_textured.glsl", F_SHADER_PATH[] = "shaders/fragment_textured.glsl";

constexpr float MILLISECONDS_IN_SECOND = 1000.0;
constexpr char SPRITESHEET_FILEPATH[] = "assets/images/player.png",
               MAP_TILESET_FILEPATH[] = "assets/images/Tile_10.png",
               BGM_FILEPATH[] = "assets/audio/galaxyloop.mp3",
               JUMP_SFX_FILEPATH[] = "assets/audio/jump.wav";

constexpr int NUMBER_OF_TEXTURES = 1;
constexpr GLint LEVEL_OF_DETAIL = 0, TEXTURE_BORDER = 0;

unsigned int LEVEL_1_DATA[] = {
    0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0,0,
    0, 2, 3, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 0,1,
    1, 2, 1, 1, 3, 3, 0, 0, 2, 1, 1, 3, 2, 1,0,
    2, 0, 1, 1, 3, 2, 1, 1, 2, 0, 0, 3, 3, 2,1,
    3, 2, 2, 3, 2, 2, 3, 3, 2, 2, 3, 3, 2, 2,0,
};

// ————— VARIABLES ————— //
GameState g_game_state;
SDL_Window* g_display_window;
AppStatus g_app_status = RUNNING;
ShaderProgram g_shader_program;
glm::mat4 g_view_matrix, g_projection_matrix;
float g_previous_ticks = 0.0f, g_accumulator = 0.0f;
GLuint g_font_texture_id;
GLuint background_texture_id;

void initialise();
void process_input();
void update();
void render();
void shutdown();

GLuint load_texture(const char* filepath) {
    int width, height, number_of_components;
    unsigned char* image = stbi_load(filepath, &width, &height, &number_of_components, STBI_rgb_alpha);
    if (image == NULL) {
        LOG("Unable to load image. Make sure the path is correct.");
        assert(false);
    }
    GLuint texture_id;
    glGenTextures(NUMBER_OF_TEXTURES, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, LEVEL_OF_DETAIL, GL_RGBA, width, height, TEXTURE_BORDER, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    stbi_image_free(image);
    return texture_id;
}

void draw_text(ShaderProgram* program, GLuint font_texture_id, std::string text,
    float font_size, float spacing, glm::vec3 position)
{
    // Scale the size of the fontbank in the UV-plane
    // We will use this for spacing and positioning
    float width = 1.0f / FONTBANK_SIZE;
    float height = 1.0f / FONTBANK_SIZE;

    // Instead of having a single pair of arrays, we'll have a series of pairs—one for
    // each character. Don't forget to include <vector>!
    std::vector<float> vertices;
    std::vector<float> texture_coordinates;

    // For every character...
    for (int i = 0; i < text.size(); i++) {
        // 1. Get their index in the spritesheet, as well as their offset (i.e. their
        //    position relative to the whole sentence)
        int spritesheet_index = (int)text[i];  // ascii value of character
        float offset = (font_size + spacing) * i;

        // 2. Using the spritesheet index, we can calculate our U- and V-coordinates
        float u_coordinate = (float)(spritesheet_index % FONTBANK_SIZE) / FONTBANK_SIZE;
        float v_coordinate = (float)(spritesheet_index / FONTBANK_SIZE) / FONTBANK_SIZE;

        // 3. Inset the current pair in both vectors
        vertices.insert(vertices.end(), {
            offset + (-0.5f * font_size), 0.5f * font_size,
            offset + (-0.5f * font_size), -0.5f * font_size,
            offset + (0.5f * font_size), 0.5f * font_size,
            offset + (0.5f * font_size), -0.5f * font_size,
            offset + (0.5f * font_size), 0.5f * font_size,
            offset + (-0.5f * font_size), -0.5f * font_size,
            });

        texture_coordinates.insert(texture_coordinates.end(), {
            u_coordinate, v_coordinate,
            u_coordinate, v_coordinate + height,
            u_coordinate + width, v_coordinate,
            u_coordinate + width, v_coordinate + height,
            u_coordinate + width, v_coordinate,
            u_coordinate, v_coordinate + height,
            });
    }

    // 4. And render all of them using the pairs
    glm::mat4 model_matrix = glm::mat4(1.0f);
    model_matrix = glm::translate(model_matrix, position);

    program->set_model_matrix(model_matrix);
    glUseProgram(program->get_program_id());

    glVertexAttribPointer(program->get_position_attribute(), 2, GL_FLOAT, false, 0,
        vertices.data());
    glEnableVertexAttribArray(program->get_position_attribute());
    glVertexAttribPointer(program->get_tex_coordinate_attribute(), 2, GL_FLOAT, false, 0,
        texture_coordinates.data());
    glEnableVertexAttribArray(program->get_tex_coordinate_attribute());

    glBindTexture(GL_TEXTURE_2D, font_texture_id);
    glDrawArrays(GL_TRIANGLES, 0, (int)(text.size() * 6));

    glDisableVertexAttribArray(program->get_position_attribute());
    glDisableVertexAttribArray(program->get_tex_coordinate_attribute());
}

void initialise() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    g_display_window = SDL_CreateWindow(GAME_WINDOW_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(g_display_window);
    SDL_GL_MakeCurrent(g_display_window, context);
    if (context == nullptr) {
        LOG("ERROR: Could not create OpenGL context.\n");
        shutdown();
    }
#ifdef _WINDOWS
    glewInit();
#endif
    glViewport(VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    g_shader_program.load(V_SHADER_PATH, F_SHADER_PATH);
    g_view_matrix = glm::mat4(1.0f);
    g_projection_matrix = glm::ortho(-5.0f, 5.0f, -3.75f, 3.75f, -1.0f, 1.0f);
    g_shader_program.set_projection_matrix(g_projection_matrix);
    g_shader_program.set_view_matrix(g_view_matrix);
    glUseProgram(g_shader_program.get_program_id());
    glClearColor(BG_RED, BG_BLUE, BG_GREEN, BG_OPACITY);
    
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096);
    
    g_game_state.bgm = Mix_LoadMUS(BGM_FILEPATH);
    
    Mix_PlayMusic(g_game_state.bgm, -1);
    
  
    Mix_VolumeMusic(MIX_MAX_VOLUME / 16.0f);
    
    g_game_state.jump_sfx = Mix_LoadWAV(JUMP_SFX_FILEPATH);
    

    
    GLuint map_texture_id = load_texture(MAP_TILESET_FILEPATH);
    g_game_state.map = new Map(LEVEL1_WIDTH, LEVEL1_HEIGHT, LEVEL_1_DATA, map_texture_id, 1.0f, 2, 2);

    GLuint player_texture_id = load_texture(SPRITESHEET_FILEPATH);
    int player_walking_animation[4][4] = { { 1, 5, 9, 13 }, { 3, 7, 11, 15 }, { 2, 6, 10, 14 }, { 0, 4, 8, 12 } };
    glm::vec3 acceleration = glm::vec3(0.0f, -4.905f, 0.0f);

    g_game_state.player = new Entity(player_texture_id,4.0f,acceleration,6.0f, player_walking_animation,0.0f,4,0,4,4,0.50f,0.50f,PLAYER);
    g_game_state.player->set_scale(glm::vec3(1.0f, 1.0f, 0.0f));

    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096);
    g_game_state.bgm = Mix_LoadMUS(BGM_FILEPATH);
    g_game_state.jump_sfx = Mix_LoadWAV(JUMP_SFX_FILEPATH);

    // ————— ENEMY SET-UP ————— //
    GLuint enemy_texture_id = load_texture("assets/images/enemy.png");
    g_font_texture_id = load_texture("assets/fonts/font1.png");
    background_texture_id = load_texture("assets/images/background.png");

    g_game_state.enemies = new Entity[ENEMY_COUNT];

    // Set up each enemy with a small range of motion
    g_game_state.enemies[0] = Entity(enemy_texture_id, 1.0f, 0.7f, 0.7f, ENEMY, SPINNER, IDLE); // Spinning enemy
    g_game_state.enemies[0].set_jumping_power(0.0f);
    g_game_state.enemies[0].set_scale(glm::vec3(1.0f, 1.0f, 0.0f));
    g_game_state.enemies[0].set_position(glm::vec3(1.0f, -1.0f, 0.0f)); // Spinner
    
    g_game_state.enemies[1] = Entity(enemy_texture_id, 1.0f, 1.0f, 0.7f, ENEMY, VERTICAL_MOVER, WALKING);
    g_game_state.enemies[1].set_jumping_power(0.0f);

    g_game_state.enemies[1].set_position(glm::vec3(10.0f, 1.4f, 0.0f)); // Vertical mover
    g_game_state.enemies[1].set_scale(glm::vec3(0.75f, 0.75f, 0.0f));
    g_game_state.enemies[2].set_scale(glm::vec3(0.75f, 0.75f, 0.0f));
    
    
    g_game_state.enemies[2] = Entity(enemy_texture_id, 1.0f, 0.5f, 0.5f, ENEMY, JUMPER, IDLE);
    g_game_state.enemies[2].set_position(glm::vec3(8.0f, 5.0f, 0.0f));; //JUMPER
    
    g_game_state.enemies[2].set_movement(glm::vec3(0.0f));
    g_game_state.enemies[2].set_acceleration(glm::vec3(0.0f, -4.0f, 0.0f));
    g_game_state.enemies[2].set_jumping_power(1.5f);


    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


bool is_enemy_nearby(Entity* player, Entity* enemy) {
    const float proximity_threshold = 1.5f; 
    return glm::distance(player->get_position(), enemy->get_position()) <= proximity_threshold;
}

void handle_enemy_collision(int enemy_index) {
    g_game_state.enemies[enemy_index].deactivate();
    g_game_state.enemies[enemy_index].set_position(glm::vec3(-100.0f, -100.0f, 0.0f)); //moveo ff
}

void process_input() {
    g_game_state.player->set_movement(glm::vec3(0.0f));
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
            case SDL_WINDOWEVENT_CLOSE:
                g_app_status = TERMINATED;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_q:
                        g_app_status = TERMINATED;
                        break;
                    case SDLK_SPACE:
                        if (g_game_state.player->get_collided_bottom()) {
                            g_game_state.player->jump();
                            Mix_PlayChannel(-1, g_game_state.jump_sfx, 0);
                        }
                        break;
                    case SDLK_a:
                        for (int i = 0; i < ENEMY_COUNT; i++) {
                            if (g_game_state.enemies[i].is_active() &&
                                is_enemy_nearby(g_game_state.player, &g_game_state.enemies[i])) {
                                handle_enemy_collision(i);
                            }
                        }
                        break;
                    default:
                        break;
                }
            default:
                break;
        }
    }
    const Uint8 *key_state = SDL_GetKeyboardState(NULL);
    if (key_state[SDL_SCANCODE_LEFT]) g_game_state.player->move_left();
    else if (key_state[SDL_SCANCODE_RIGHT]) g_game_state.player->move_right();
    if (glm::length(g_game_state.player->get_movement()) > 1.0f) g_game_state.player->normalise_movement();
}


void update() {
    float ticks = (float)SDL_GetTicks() / MILLISECONDS_IN_SECOND;
    float delta_time = ticks - g_previous_ticks;
    g_previous_ticks = ticks;
    delta_time += g_accumulator;
    
    if (delta_time < FIXED_TIMESTEP) {
        g_accumulator = delta_time;
        return;
    }

    while (delta_time >= FIXED_TIMESTEP) {
        g_game_state.player->update(FIXED_TIMESTEP, g_game_state.player, NULL, 0, g_game_state.map);
        
        for (int i = 0; i < ENEMY_COUNT; i++) {
            if (g_game_state.enemies[i].is_active()) {
                g_game_state.enemies[i].update(FIXED_TIMESTEP, g_game_state.player, NULL, 0, g_game_state.map);

                if (g_game_state.player->check_collision(&g_game_state.enemies[i])) {
                    handle_enemy_collision(i);
                }
            }
        }
        delta_time -= FIXED_TIMESTEP;
    }
    
    g_accumulator = delta_time;
    g_view_matrix = glm::mat4(1.0f);
    g_view_matrix = glm::translate(g_view_matrix, glm::vec3(-g_game_state.player->get_position().x, 0.0f, 0.0f));
}

void render() {
    glClear(GL_COLOR_BUFFER_BIT);
    g_shader_program.set_view_matrix(glm::mat4(1.0f));

    glm::mat4 background_model_matrix = glm::mat4(1.0f);
    g_shader_program.set_model_matrix(background_model_matrix);

    glBindTexture(GL_TEXTURE_2D, background_texture_id);
    float background_vertices[] = {
        -6.0f,  4.0f,    6.0f,  4.0f,    6.0f, -4.0f,
        -6.0f,  4.0f,    6.0f, -4.0f,   -6.0f, -4.0f
    };

    float background_tex_coords[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };

    glVertexAttribPointer(g_shader_program.get_position_attribute(), 2, GL_FLOAT, false, 0, background_vertices);
    glEnableVertexAttribArray(g_shader_program.get_position_attribute());
    glVertexAttribPointer(g_shader_program.get_tex_coordinate_attribute(), 2, GL_FLOAT, false, 0, background_tex_coords);
    glEnableVertexAttribArray(g_shader_program.get_tex_coordinate_attribute());

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(g_shader_program.get_position_attribute());
    glDisableVertexAttribArray(g_shader_program.get_tex_coordinate_attribute());

    g_shader_program.set_view_matrix(g_view_matrix);

    g_game_state.map->render(&g_shader_program);
    g_game_state.player->render(&g_shader_program);

    for (int i = 0; i < ENEMY_COUNT; i++) {
        g_game_state.enemies[i].render(&g_shader_program);
    }

    if (!g_game_state.player->is_active()) {
        glm::vec3 player_position = g_game_state.player->get_position();
        draw_text(&g_shader_program, g_font_texture_id, "YOU LOSE", 0.9f, 0.15f,
                  glm::vec3(player_position.x - 2.5f, player_position.y + 0.5f, 0.0f));
        SDL_GL_SwapWindow(g_display_window);
        SDL_Delay(3000);
        g_app_status = TERMINATED;
        return;
    }

    bool all_enemies_defeated = true;
    for (int i = 0; i < ENEMY_COUNT; i++) {
        if (g_game_state.enemies[i].is_active()) {
            all_enemies_defeated = false;
            break;
        }
    }

    if (all_enemies_defeated) {
        glm::vec3 player_position = g_game_state.player->get_position();
        draw_text(&g_shader_program, g_font_texture_id, "YOU WIN", 0.9f, 0.15f,
                  glm::vec3(player_position.x - 2.5f, player_position.y + 0.5f, 0.0f));
        SDL_GL_SwapWindow(g_display_window);
        SDL_Delay(3000);
        g_app_status = TERMINATED;
        return;
    }

    SDL_GL_SwapWindow(g_display_window);
}




void shutdown() {
    SDL_Quit();
    delete [] g_game_state.enemies;
    delete g_game_state.player;
    delete g_game_state.map;
    Mix_FreeChunk(g_game_state.jump_sfx);
    Mix_FreeMusic(g_game_state.bgm);
}

int main(int argc, char* argv[]) {
    initialise();
    while (g_app_status == RUNNING) {
        process_input();
        update();
        render();
    }
    shutdown();
    return 0;
}


