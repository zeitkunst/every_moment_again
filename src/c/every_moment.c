// To convert from OTF to TTF:
//  fontforge -script scripts/otf2ttf.sh FONTNAME.otf 
//
// Working with more than 3-byte unicode glyphs:
// https://forums.pebble.com/t/how-can-i-filter-3-byte-unicode-characters-glyphs-in-ttf/26024
//
// TODO
// * Figure out how to properly calculate the size of the scroll window for a given font size, have to take into account descenders, need to ensure that we scroll by an integral amount of maximum height + descenders
// * I like Fell English at 24, but need to recalculate the sizes accordingly
// * Try different fonts, sans-serif fonts too, also for the time line below 
// * Try text for time too
// * Generative poem, for Lisa only, that takes pyephem data for Sun, Moon, planets, asteroids (!), and creates poem based off of that

/*
 * State machine for these poems:
 * 1. Start screen (blank for a period or so)
 * 2. Title screen (a couple of periods)
 * 3. Blank screen (for a period or two)
 * 4. Poem, scroll from top to bottom (we might want to split out to another state machine here, for the scrolling, and/or for the scrolling of individual screens if we decide to do things that way)
 * 5. Blank screen (a couple of periods)
 * 6. Restart
 */

#include <pebble.h>
#define TIMER_PERIOD 500 // number of milliseconds before we check our period again

// State machine variables for satellite poem
typedef enum
{
    STATE_START = 0,
    STATE_TITLE,
    STATE_BLANK_1,
    STATE_POEM,
    STATE_BLANK_2
} stars_state_t;

uint8_t state_periods[] = {
    1,
    3,
    1,
    0, // we don't move forward in STATE_POEM based on periods
    1
};

stars_state_t stars_state = STATE_START;
static uint8_t current_period = 0;

// Margin for text, in pixels
uint8_t margin = 4;

// Layers and fonts
static Window *s_main_window;
static Layer *window_layer;

static TextLayer *s_time_layer;
static GFont s_time_font;

static TextLayer *s_title_layer = NULL;
static GFont s_title_font;
static const char *default_title = "A POEM";

static TextLayer *s_fragment_layer = NULL;
static GFont s_fragment_font;
static const char *default_fragment = "SOME WORDS";


// Screen bounds
static GRect bounds;

// Calculations for scrolling based on font size
// Probably need to change when we change fonts for text on scroll layer
// Size 24: 24 pixels high plus around 8 for descenders, then 24 pixels for each line, and -1 to make pixel "perfect"
static int fontSize = 24; // in pixels (?)
static int descenderSize = 6; // in pixels (?)
static int numLines = 5; // total number of lines we'd like to display on screen at this font size
int scrollSize, pageScroll;

//Keeping track of state time
AppTimer *stateTimer; // Timer for checking our state
int currentStateTime = 0; // Counter for keeping track of time

// Duration of periods
static uint8_t updatePeriod = 6; // Scroll every updatePeriod seconds
//static uint8_t updatePeriod = 1; // Scroll every updatePeriod seconds
static uint8_t poemPeriod  = 10; // Update poem every poemPeriod minutes
//static uint8_t poemPeriod  = 1; // Update poem every poemPeriod minutes

// Amount of time to stay in each state, based on the granularity of the timer
int state_times[] = {
    1 * TIMER_PERIOD,
    4 * TIMER_PERIOD,
    1 * TIMER_PERIOD,
    8 * TIMER_PERIOD, 
    1 * TIMER_PERIOD
};

// Buffers for incoming information
static char every_moment_title_buffer[64] = {0};
static char title_layer_buffer[64] = {0};

static char every_moment_fragment_buffer[2048];
static char fragment_layer_buffer[2048];
char fragment[2048];

unsigned int beginning_index = 0;
unsigned int ending_index = 0;
unsigned int fragment_end = 0;


/*
 * Create title layer with optional default title
 */
static void generate_title_layer(char *title) {
    uint8_t text_height = 20 + 8 + 20 + 20 + 20 + 20;
    s_title_layer = text_layer_create(
            GRect(margin, PBL_IF_ROUND_ELSE(84 - (text_height/2), 84 - (text_height/2)), bounds.size.w - (2 * margin), text_height));

    //s_title_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ADOBE_JENSON_24));
    s_title_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHARIS_SIL_24));

    // Improve the layout to be more like a watchface
    text_layer_set_background_color(s_title_layer, GColorClear);
    text_layer_set_text_color(s_title_layer, GColorWhite);

    if (title_layer_buffer[0] == 0) {
        text_layer_set_text(s_title_layer, title);
    } else {
        text_layer_set_text(s_title_layer, title_layer_buffer);
    }
    text_layer_set_font(s_title_layer, s_title_font);
    text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);

    // Add it as a child layer to the Window's root layer
    layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
}

static void generate_fragment_layer(char *fragment) {
    uint8_t text_height = 20 + 8 + 20 + 20 + 20 + 20;
    s_fragment_layer = text_layer_create(
            GRect(margin, PBL_IF_ROUND_ELSE(70 - (text_height/2), 70 - (text_height/2)), bounds.size.w - (2 * margin), text_height));

    //s_fragment_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ADOBE_JENSON_24));
    s_fragment_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHARIS_SIL_24));

    // Improve the layout to be more like a watchface
    text_layer_set_background_color(s_fragment_layer, GColorClear);
    text_layer_set_text_color(s_fragment_layer, GColorWhite);

    if (fragment_layer_buffer[0] == 0) {
        text_layer_set_text(s_fragment_layer, fragment);
    } else {
        text_layer_set_text(s_fragment_layer, fragment_layer_buffer);
    }
    text_layer_set_font(s_fragment_layer, s_fragment_font);
    text_layer_set_text_alignment(s_fragment_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_fragment_layer, GTextOverflowModeWordWrap);

    // Add it as a child layer to the Window's root layer
    layer_add_child(window_layer, text_layer_get_layer(s_fragment_layer));
}


/*
 * Methods for showing and hiding text and scroll layers
 */
static void show_title_layer(void) {
    layer_set_hidden((Layer *)s_title_layer, false);
}

static void show_fragment_layer(void) {
    layer_set_hidden((Layer *)s_fragment_layer, false);
}


static void hide_title_layer(void) {
    layer_set_hidden((Layer *)s_title_layer, true);
}

static void hide_fragment_layer(void) {
    layer_set_hidden((Layer *)s_fragment_layer, true);
}


/*
 * Update time and write to buffer
 */
static void update_time() {
    // Get a tm structure
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    // Write the current hours and minutes into a buffer
    static char s_buffer[8];
    strftime(s_buffer, sizeof(s_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);

    // Display this time on the TextLayer
    text_layer_set_text(s_time_layer, s_buffer);
}

/**
 * Timer that gets called every TIMER_PERIOD milliseconds to deal with state changes
 */
static void stateTimerCallback(void *data) {

    switch (stars_state) {
        case STATE_START:
            if (currentStateTime < state_times[STATE_START]) {

                currentStateTime += TIMER_PERIOD;
            } else {
                currentStateTime = 0;
                show_title_layer();
                stars_state = STATE_TITLE;
            }
            break;
        case STATE_TITLE:
            if (currentStateTime < state_times[STATE_TITLE]) {
                currentStateTime += TIMER_PERIOD;
            } else {
                currentStateTime = 0;
                hide_title_layer();
                stars_state = STATE_BLANK_1;
                updatePeriod = 4;
            }
            break;
        case STATE_BLANK_1:
            if (currentStateTime < state_times[STATE_BLANK_1]) {
                currentStateTime += TIMER_PERIOD;
            } else {
                currentStateTime = 0;

                text_layer_set_text(s_fragment_layer, fragment);
                show_fragment_layer();
                stars_state = STATE_POEM;
                updatePeriod = 6;
            }

            break;
        case STATE_POEM:

            // Before scrolling, we could probably change our updatePeriod
            // to something different than the other periods
            if (currentStateTime < state_times[STATE_POEM]) {
                currentStateTime += TIMER_PERIOD;
            } else {
                if (fragment_end) {
                    fragment_end = 0;
                    hide_fragment_layer();
                    beginning_index = 0;
                    ending_index = 0;
                    memset(&fragment[0], 0, sizeof(fragment));
                    stars_state = STATE_BLANK_2;
                    updatePeriod = 4;
                    break;
                } else {
                    for (unsigned int i = beginning_index; i <= strlen(fragment_layer_buffer); i++) {
                    if (fragment_layer_buffer[i] == '|') {
                        ending_index = i;
                        memset(&fragment[0], 0, sizeof(fragment));
                        APP_LOG(APP_LOG_LEVEL_INFO, "ending_index, %d", ending_index);


                        for (unsigned int j = beginning_index; j < ending_index; j++) {
                            fragment[j - beginning_index] = fragment_layer_buffer[j];
                        }
                        fragment[ending_index - beginning_index + 1] = '\0';
                        APP_LOG(APP_LOG_LEVEL_INFO, "FRAGMENT: %s", fragment);
                        APP_LOG(APP_LOG_LEVEL_INFO, "INDICES: %d, %d", beginning_index, ending_index);
        
                        beginning_index = ending_index + 1;

                        text_layer_set_text(s_fragment_layer, fragment);

                        break;
                    } else if (fragment_layer_buffer[i] == '\0') {
                        memset(&fragment[0], 0, sizeof(fragment));
                        for (unsigned int j = beginning_index; j < strlen(fragment_layer_buffer); j++) {
                            fragment[j - beginning_index] = fragment_layer_buffer[j];

                        }
                        fragment[strlen(fragment_layer_buffer) - beginning_index + 1] = '\0';
                        text_layer_set_text(s_fragment_layer, fragment);
                        fragment_end = 1;
                        break;

                        }

                    }
                }

                /*
                fragment = strtok(fragment_layer_buffer, "\n");
                if (fragment != NULL) {
                    text_layer_set_text(s_fragment_layer, fragment);
                } else {
                    stars_state = STATE_BLANK_2;
                }
                */
                currentStateTime = 0;
            }


            break;
        case STATE_BLANK_2:
            if (currentStateTime < state_times[STATE_BLANK_2]) {
                currentStateTime += TIMER_PERIOD;
            } else {
                currentStateTime = 0;
                stars_state = STATE_START;
                //snprintf(fragment_layer_buffer, sizeof(fragment_layer_buffer), "%s", every_moment_fragment_buffer);
                updatePeriod = 4;
            }

            break;
        
    }



    // Register timer for next period
    stateTimer = app_timer_register(TIMER_PERIOD, (AppTimerCallback) stateTimerCallback, NULL);
}


/*
 * Main handler for moving through our state machine, updating time, etc.
 */
static void tick_handler_seconds(struct tm *tick_time, TimeUnits units_changed) {

    // Update time every minute    
    if (tick_time->tm_sec % 60 == 0) {
        update_time();
        APP_LOG(APP_LOG_LEVEL_INFO, "updating time");
    }


    // Update poem every poemPeriod minutes
    // Check the minute
    if (tick_time->tm_min % poemPeriod == 0) {
        // and then ensure that we only do this once during the minute
        if (tick_time->tm_sec % 60 == 0) {
            // Begin dictionary
            DictionaryIterator *iter;
            app_message_outbox_begin(&iter);
    
            // Add a key-value pair
            dict_write_uint8(iter, 0, 0);
    
            app_message_outbox_send();

            APP_LOG(APP_LOG_LEVEL_INFO, "Updating poem");
        }
    }

}

/*
 * Set up window, layers, and fonts
 */
static void main_window_load(Window *window) {

    // Get information about the window
    window_layer = window_get_root_layer(window);
    bounds = layer_get_frame(window_layer);
    GRect max_text_bounds = GRect(0, 0, bounds.size.w - (margin * 2), 2000);

    // Create time GFont
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ANDIKA_20));

    // Create poem font
    s_fragment_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_CHARIS_SIL_24));

    generate_title_layer("Every moment again");
    hide_title_layer();

    generate_fragment_layer("A FRAGMENT");
    hide_fragment_layer();

    // Create default title
    snprintf(title_layer_buffer, sizeof(title_layer_buffer), "%s", default_title);

    // Register state timer callback
    stateTimer = app_timer_register(TIMER_PERIOD, (AppTimerCallback) stateTimerCallback, NULL);
    
    // Create the text layer with specific bounds
    s_time_layer = text_layer_create(
            GRect(margin, PBL_IF_ROUND_ELSE(144, 144), bounds.size.w - (2 * margin), 20));

    // Improve the layout to be more like a watchface
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_text(s_time_layer, "00:00");
    //text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

    // Add it as a child layer to the Window's root layer
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));


}

/*
 * Destory variables so that we don't leak memory
 */
static void main_window_unload(Window *window) {
    // Destory TextLayer
    text_layer_destroy(s_time_layer);

    // Unload GFont
    fonts_unload_custom_font(s_time_font);

    // Destroy poem elements
    text_layer_destroy(s_fragment_layer);
    fonts_unload_custom_font(s_fragment_font);


    // Destroy title elements
    text_layer_destroy(s_title_layer);
    fonts_unload_custom_font(s_title_font);

    // Destroy timer
    app_timer_cancel(stateTimer);

}

/*
 * Handle incoming data from phone/javascript
 */
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    // Read tuples for data
    Tuple *every_moment_poem_tuple = dict_find(iterator, MESSAGE_KEY_EVERY_MOMENT_POEM);
    Tuple *every_moment_title_tuple = dict_find(iterator, MESSAGE_KEY_EVERY_MOMENT_TITLE);


    if (every_moment_poem_tuple && every_moment_title_tuple) {
        snprintf(every_moment_fragment_buffer, sizeof(every_moment_fragment_buffer), "%s", every_moment_poem_tuple->value->cstring);
        snprintf(every_moment_title_buffer, sizeof(every_moment_title_buffer), "%s", every_moment_title_tuple->value->cstring);


        // Assemble string and display
        snprintf(fragment_layer_buffer, sizeof(fragment_layer_buffer), "%s", every_moment_fragment_buffer);
        snprintf(fragment_layer_buffer, sizeof(fragment_layer_buffer), "%s", every_moment_fragment_buffer);
        APP_LOG(APP_LOG_LEVEL_INFO, "Poem: %s", fragment_layer_buffer);

        // Assemble title string
        snprintf(title_layer_buffer, sizeof(title_layer_buffer), "%s", every_moment_title_buffer);
        text_layer_set_text(s_title_layer, title_layer_buffer);

        // Create first fragment
        for (unsigned int i = beginning_index; i < strlen(fragment_layer_buffer); i++) {
            if (fragment_layer_buffer[i] == '|') {
                ending_index = i;
                memset(&fragment[0], 0, sizeof(fragment));
                for (unsigned int j = beginning_index; j < ending_index; j++) {
                    fragment[j - beginning_index] = fragment_layer_buffer[j];
                }
                fragment[ending_index - beginning_index + 1] = '\0';

                text_layer_set_text(s_fragment_layer, fragment);
                APP_LOG(APP_LOG_LEVEL_INFO, "FRAGMENT: %s", fragment);
                APP_LOG(APP_LOG_LEVEL_INFO, "INDICES: %d, %d", beginning_index, ending_index);

                beginning_index = ending_index + 1;
                break;
            }
        }
        //fragment = strtok(fragment_layer_buffer_mutable, "|");

    }
}

/*
 * Deal with callback and inbox issues
 */
static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped! Reason: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

/*
 * Initialize window, callbacks, handlers
 */
static void init() {
    s_main_window = window_create();

    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });    

    window_set_background_color(s_main_window, GColorBlack);

    // Only one subscription to this service is allowed
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler_seconds);

    window_stack_push(s_main_window, true);

    // Register callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);
    app_message_register_outbox_failed(outbox_failed_callback);
    app_message_register_outbox_sent(outbox_sent_callback);

    // Open AppMessage
    const int inbox_size = 4096;
    const int outbox_size = 4096;
    app_message_open(inbox_size, outbox_size);

    // Make sure the time is displayed from the start
    update_time();
}

/*
 * Destroy window
 */
static void deinit() {
    // Destroy window
    window_destroy(s_main_window);
}

/*
 * Main app loop
 */
int main(void) {
    init();
    app_event_loop();
    deinit();
}
