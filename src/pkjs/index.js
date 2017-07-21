var xhrRequest = function(url, type, callback) {
    var xhr = new XMLHttpRequest();
    xhr.onload = function() {
        callback(this.responseText);
    };
    xhr.open(type, url);
    xhr.send();
};


function locationSuccess(pos) {

    //var satsURL = 'http://api.zeitkunst.org/sats/pebble/poem/42.294615,71.302342,185';
    // Give longitude in E longitude, coordinate change happens on the server
    // TODO
    // Check and see if I need to do a time zone offset, like I do in satellite poems...I don't think I do, but I should check
    var starsURL = 'http://api.zeitkunst.org/planets_stars_every_moment/pebble/poem/' + pos.coords.latitude + "," + pos.coords.longitude;

    // Send request to OpenWeatherMap
    xhrRequest(starsURL, 'GET',
        function(responseText) {
            // responseText contains a JSON object with weather info
            var stars = JSON.parse(responseText);

            console.log("Lat, long: " + pos.coords.latitude + ", " + pos.coords.longitude);
            //console.log(stars["dappled_void_title"]);
            //console.log(stars["dappled_void_poem"]);


            // Assemble dictionary using our keys
            var dictionary = {
                "EVERY_MOMENT_TITLE":stars["every_moment_title"],
                "EVERY_MOMENT_POEM":stars["every_moment_poem"]
            };

            // Send to Pebble
            Pebble.sendAppMessage(dictionary,
                function(e) {
                    console.log("Poem sent to Pebble successfully!");
                },
                function(e) {
                    console.log("Error sending poem to Pebble: " + JSON.stringify(e));
                }
            );


        }
    );

}

function locationError(err) {
    console.log("Error requesting location!");
}

function getWeather() {
    navigator.geolocation.getCurrentPosition(
        locationSuccess,
        locationError,
        {timeout: 15000, maximumAge: 60000}
    );
}

// Listen for when the watchface is opened
Pebble.addEventListener('ready',
    function(e) {
        console.log('PebbleKit JS ready!');

        // Get the initial weather
        getWeather();
    }
);

// Listen for when an AppMessage is received
Pebble.addEventListener('appmessage',
    function(e) {
        console.log('AppMessage received!');
        getWeather();
    }
);

