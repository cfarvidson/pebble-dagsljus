var xhrRequest = function(url, type, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function() { callback(this.responseText); };
  xhr.open(type, url);
  xhr.send();
};

function sendSunAndWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat +
    '&longitude=' + lon +
    '&daily=sunrise,sunset' +
    '&hourly=precipitation_probability' +
    '&timezone=auto' +
    '&forecast_days=1';

  xhrRequest(url, 'GET', function(responseText) {
    var data = JSON.parse(responseText);
    var srParts = data.daily.sunrise[0].split('T')[1].split(':');
    var ssParts = data.daily.sunset[0].split('T')[1].split(':');
    var srMin = parseInt(srParts[0], 10) * 60 + parseInt(srParts[1], 10);
    var ssMin = parseInt(ssParts[0], 10) * 60 + parseInt(ssParts[1], 10);

    var rainBitmask = 0;
    var probs = data.hourly.precipitation_probability;
    for (var h = 0; h < 24 && h < probs.length; h++) {
      if (probs[h] > 50) rainBitmask |= (1 << h);
    }

    Pebble.sendAppMessage(
      { 'SUNRISE': srMin, 'SUNSET': ssMin, 'RAIN_HOURS': rainBitmask },
      function() { console.log('Weather sent: rise=' + srMin + ' set=' + ssMin + ' rain=0x' + rainBitmask.toString(16)); },
      function(e) { console.log('Failed to send weather: ' + JSON.stringify(e)); }
    );
  });
}

// Resolve the configured city to coordinates (cached per city name).
function geocodeCity(city, callback) {
  if (localStorage.getItem('geoCity') === city) {
    callback(parseFloat(localStorage.getItem('geoLat')), parseFloat(localStorage.getItem('geoLon')));
    return;
  }
  var url = 'https://geocoding-api.open-meteo.com/v1/search?count=1&name=' + encodeURIComponent(city);
  xhrRequest(url, 'GET', function(responseText) {
    var data = JSON.parse(responseText);
    if (!data.results || !data.results.length) {
      console.log('City not found: ' + city);
      return;
    }
    var r = data.results[0];
    localStorage.setItem('geoCity', city);
    localStorage.setItem('geoLat', r.latitude);
    localStorage.setItem('geoLon', r.longitude);
    console.log('Geocoded ' + city + ' -> ' + r.name + ', ' + r.country + ' (' + r.latitude + ', ' + r.longitude + ')');
    callback(r.latitude, r.longitude);
  });
}

function fetchSunAndWeather() {
  var city = currentSettings().city;
  if (city) {
    geocodeCity(city, sendSunAndWeather);
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function(pos) { sendSunAndWeather(pos.coords.latitude, pos.coords.longitude); },
    function(err) { console.log('Geolocation unavailable: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
}

function currentSettings() {
  return {
    use12h: localStorage.getItem('use12h') !== 'false',
    showRain: localStorage.getItem('showRain') !== 'false',
    city: localStorage.getItem('city') || ''
  };
}

function sendSettings(cfg) {
  Pebble.sendAppMessage(
    {
      'USE_12H': cfg.use12h ? 1 : 0,
      'SHOW_RAIN': cfg.showRain ? 1 : 0
    },
    function() { console.log('Settings sent: ' + JSON.stringify(cfg)); },
    function(err) { console.log('Failed to send settings: ' + JSON.stringify(err)); }
  );
}

var CONFIG_HTML =
  '<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width">' +
  '<style>body{font-family:-apple-system,sans-serif;margin:20px;background:#f5f5f5;color:#111}' +
  '.card{background:#fff;border-radius:8px;padding:16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.12)}' +
  '.row{display:flex;justify-content:space-between;align-items:center}' +
  'label{font-size:16px}input[type=checkbox]{width:22px;height:22px}' +
  'input[type=text]{width:100%;box-sizing:border-box;margin-top:8px;padding:10px;font-size:16px;border:1px solid #ccc;border-radius:6px}' +
  '.hint{font-size:13px;color:#666;margin-top:6px}' +
  'button{width:100%;padding:14px;background:#007aff;color:#fff;border:none;border-radius:8px;font-size:16px;margin-top:16px}' +
  '</style></head><body><h2>Dagsljus</h2>' +
  '<div class="card"><label for="city">Location</label>' +
  '<input type="text" id="city" placeholder="e.g. Stockholm">' +
  '<div class="hint">City for sunrise/sunset. Leave empty to use the phone\'s location.</div></div>' +
  '<div class="card"><div class="row"><label for="use12h">12-hour numerals</label>' +
  '<input type="checkbox" id="use12h"></div></div>' +
  '<div class="card"><div class="row"><label for="showRain">Show rain overlay</label>' +
  '<input type="checkbox" id="showRain"></div></div>' +
  '<button onclick="submit()">Save</button>' +
  '<script>' +
  'var o=JSON.parse(decodeURIComponent(location.hash.substring(1))||"{}");' +
  'document.getElementById("city").value=o.city||"";' +
  'document.getElementById("use12h").checked=!!o.use12h;' +
  'document.getElementById("showRain").checked=!!o.showRain;' +
  'function submit(){var r={city:document.getElementById("city").value.trim(),' +
  'use12h:document.getElementById("use12h").checked,' +
  'showRain:document.getElementById("showRain").checked};' +
  'document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(r))}' +
  '</script></body></html>';

Pebble.addEventListener('ready', function() {
  sendSettings(currentSettings());
  fetchSunAndWeather();
});

Pebble.addEventListener('showConfiguration', function() {
  var hash = encodeURIComponent(JSON.stringify(currentSettings()));
  Pebble.openURL('data:text/html,' + encodeURIComponent(CONFIG_HTML) + '#' + hash);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var cfg = JSON.parse(decodeURIComponent(e.response));
  localStorage.setItem('use12h', cfg.use12h);
  localStorage.setItem('showRain', cfg.showRain);
  localStorage.setItem('city', cfg.city || '');
  sendSettings(cfg);
  fetchSunAndWeather();
});
