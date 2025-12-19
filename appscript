function doGet(e) {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(10000))
    return ContentService.createTextOutput("BUSY");

  try {
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = ss.getSheetByName("Sheet1");
    var userSheet = ss.getSheetByName("Sheet2"); // your Sheet2

    var rawUid = (e.parameter.uid || "").trim();
    if (!rawUid)
      return ContentService.createTextOutput("ERROR_NO_UID");

    // Clean UID (ABCDE format)
    var uid = rawUid.toUpperCase().replace(/[^A-F0-9]/g, "");

    // Convert to "XX XX XX XX"
    var spacedUid = uid.match(/.{1,2}/g).join(" ");

    // ----------------------------------------------------
    // FETCH USERS FROM SHEET2
    // ----------------------------------------------------
    var lastUserRow = userSheet.getLastRow();
    var userData = userSheet.getRange(2, 1, lastUserRow - 1, 2).getValues();

    var name = "UNKNOWN";

    for (var i = 0; i < userData.length; i++) {
      var sheetUid = (userData[i][0] || "").toString().trim().toUpperCase();
      if (sheetUid === spacedUid) {
        name = (userData[i][1] || "").toString().trim();
        break;
      }
    }

    // ----------------------------------------------------
    // TIME SETTINGS
    // ----------------------------------------------------
    var timezone = "Asia/Kolkata";
    var now = new Date();
    var todayDate = Utilities.formatDate(now, timezone, "dd/MM/yyyy");
    var timestamp = Utilities.formatDate(now, timezone, "HH:mm:ss");

    // ----------------------------------------------------
    // SEARCH FOR EXISTING OPEN SESSION
    // ----------------------------------------------------
    var lastRow = sheet.getLastRow();

    if (lastRow < 2) {
      sheet.appendRow([name, todayDate, timestamp, "", "", "", spacedUid]);
      return ContentService.createTextOutput("Welcome " + name);
    }

    var data = sheet.getRange(2, 1, lastRow - 1, 7).getValues();

    for (var i = data.length - 1; i >= 0; i--) {
      var rowDate = formatDate(data[i][1]);
      var rowUid = (data[i][6] || "").toString().trim().toUpperCase();
      var rowTimeOut = data[i][3];

      if (rowUid === spacedUid && rowDate === todayDate && rowTimeOut === "") {
        var rowIndex = i + 2;
        sheet.getRange(rowIndex, 4).setValue(timestamp);

        var timeIn = data[i][2];
        var worked = calculateDuration(timeIn, timestamp, todayDate);
        sheet.getRange(rowIndex, 5).setValue(worked);

        return ContentService.createTextOutput("Goodbye " + name);
      }
    }

    // ----------------------------------------------------
    // NEW ENTRY
    // ----------------------------------------------------
    sheet.appendRow([name, todayDate, timestamp, "", "", "", spacedUid]);
    return ContentService.createTextOutput("Welcome " + name);

  } catch (err) {
    return ContentService.createTextOutput("Error: " + err.message);

  } finally {
    lock.releaseLock();
  }
}

// Helper
function formatDate(dateObj) {
  if (!dateObj) return "";
  if (typeof dateObj === "string") return dateObj;
  return Utilities.formatDate(new Date(dateObj), "Asia/Kolkata", "dd/MM/yyyy");
}

function calculateDuration(timeInStr, timeOutStr, dateStr) {
  var dateParts = dateStr.split("/");
  var isoDate = dateParts[2] + "/" + dateParts[1] + "/" + dateParts[0];

  var t1;
  if (timeInStr instanceof Date) {
    t1 = timeInStr;
    t1.setFullYear(dateParts[2], dateParts[1] - 1, dateParts[0]);
  } else {
    t1 = new Date(isoDate + " " + timeInStr);
  }

  var t2 = new Date(isoDate + " " + timeOutStr);

  if (t2 < t1) return "Error";

  var diff = t2 - t1;
  var hours = Math.floor(diff / 36e5);
  var minutes = Math.floor((diff % 36e5) / 6e4);

  return hours + "h " + minutes + "m";
}
