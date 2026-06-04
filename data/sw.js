/* ================================================================
   Smart Bell — Service Worker
   Handles background bell notifications
   File: data/sw.js
   ================================================================ */

var SW_VERSION = '1.0';
var scheduledTimers = [];
var storedEvents = [];
var storedHolidays = [];
var advanceMinutes = 5;

/* ── Install & Activate ── */
self.addEventListener('install', function(e) {
  self.skipWaiting();
});

self.addEventListener('activate', function(e) {
  e.waitUntil(self.clients.claim());
});

/* ── Message from main page ── */
self.addEventListener('message', function(e) {
  var data = e.data;
  if (!data || !data.type) return;

  switch (data.type) {

    case 'SCHEDULE':
      storedEvents    = data.events    || [];
      storedHolidays  = data.holidays  || [];
      advanceMinutes  = data.advance   || 5;
      cancelAllTimers();
      scheduleNotifications();
      e.source.postMessage({
        type: 'SCHEDULE_ACK',
        count: scheduledTimers.length
      });
      break;

    case 'CANCEL':
      cancelAllTimers();
      storedEvents   = [];
      storedHolidays = [];
      e.source.postMessage({ type: 'CANCEL_ACK' });
      break;

    case 'PING':
      e.source.postMessage({
        type: 'PONG',
        version: SW_VERSION,
        scheduled: scheduledTimers.length,
        events: storedEvents.length
      });
      break;
  }
});

/* ── Notification click — focus or open app ── */
self.addEventListener('notificationclick', function(e) {
  e.notification.close();
  e.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true })
      .then(function(clientList) {
        for (var i = 0; i < clientList.length; i++) {
          var client = clientList[i];
          if (client.url && client.focus) {
            return client.focus();
          }
        }
        if (self.clients.openWindow) {
          return self.clients.openWindow('/');
        }
      })
  );
});

/* ================================================================
   CORE SCHEDULING LOGIC
   ================================================================ */

function cancelAllTimers() {
  scheduledTimers.forEach(function(t) { clearTimeout(t); });
  scheduledTimers = [];
}

function scheduleNotifications() {
  if (!storedEvents || storedEvents.length === 0) return;

  var now        = new Date();
  var advanceMs  = advanceMinutes * 60 * 1000;
  var maxDays    = 7;
  var scheduled  = 0;

  /* Check each of the next 7 days */
  for (var dayOffset = 0; dayOffset < maxDays; dayOffset++) {

    var checkDate = new Date(now);
    checkDate.setDate(checkDate.getDate() + dayOffset);
    checkDate.setSeconds(0, 0);

    /* Skip holidays */
    if (isHoliday(checkDate)) continue;

    var weekday = checkDate.getDay(); /* 0=Sun … 6=Sat */

    storedEvents.forEach(function(ev) {
      if (!ev.enabled) return;

      /* Check weekday mask */
      if (!(ev.weekdayMask & (1 << weekday))) return;

      /* Build the bell fire time for this day */
      var bellTime = new Date(checkDate);
      bellTime.setHours(ev.hour, ev.minute, 0, 0);

      /* Notification fires X minutes BEFORE the bell */
      var notifTime = new Date(bellTime.getTime() - advanceMs);

      /* Skip if notification time already passed */
      var delayMs = notifTime.getTime() - now.getTime();
      if (delayMs < 0) return;

      /* Cap at 24 days in ms (setTimeout max safe ~24.8 days) */
      if (delayMs > 2073600000) return;

      var evName     = ev.name || 'Bell';
      var bellHour   = String(ev.hour).padStart(2, '0');
      var bellMin    = String(ev.minute).padStart(2, '0');
      var timeStr    = bellHour + ':' + bellMin;
      var advStr     = advanceMinutes === 1
                         ? '1 minute'
                         : advanceMinutes + ' minutes';

      /* Build notification payload */
      var title   = '\uD83D\uDD14 Bell in ' + advStr;
      var body    = evName + ' \u2014 ' + timeStr;
      var tag     = 'bell-' + ev.id + '-' + bellTime.getTime();
      var dateStr = getDayLabel(checkDate, now);
      if (dateStr) body = dateStr + ' \u00B7 ' + body;

      /* Schedule it */
      var timer = setTimeout(function(t, b, tg) {
        return function() { fireNotification(t, b, tg); };
      }(title, body, tag), delayMs);

      scheduledTimers.push(timer);
      scheduled++;
    });
  }

  return scheduled;
}

/* ── Fire a single notification ── */
function fireNotification(title, body, tag) {
  self.registration.showNotification(title, {
    body    : body,
    icon    : '/icon.svg',
    badge   : '/icon.svg',
    tag     : tag,
    vibrate : [200, 100, 200],
    requireInteraction: false
  });

  /* Remove this timer from the tracked list */
  /* (already fired — just cleanup reference) */
  /* No action needed — timer already expired */
}

/* ================================================================
   HELPER FUNCTIONS
   ================================================================ */

/* Returns true if the given date is in the holiday list */
function isHoliday(date) {
  if (!storedHolidays || storedHolidays.length === 0) return false;
  var y = date.getFullYear();
  var m = String(date.getMonth() + 1).padStart(2, '0');
  var d = String(date.getDate()).padStart(2, '0');
  var dateStr = y + '-' + m + '-' + d;
  for (var i = 0; i < storedHolidays.length; i++) {
    if (storedHolidays[i].date === dateStr) return true;
  }
  return false;
}

/* Returns "Today", "Tomorrow", or day name */
function getDayLabel(date, now) {
  var todayMidnight = new Date(now);
  todayMidnight.setHours(0, 0, 0, 0);
  var dateMidnight = new Date(date);
  dateMidnight.setHours(0, 0, 0, 0);
  var diffDays = Math.round(
    (dateMidnight - todayMidnight) / 86400000
  );
  if (diffDays === 0) return 'Today';
  if (diffDays === 1) return 'Tomorrow';
  var days = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
  return days[date.getDay()];
}