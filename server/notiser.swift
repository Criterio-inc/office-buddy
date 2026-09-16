// Notiser — läser notiscentralens databas och lämnar sms, Teams och annat
// till buddyns brevlåda. Kör som egen launchd-tjänst med eget namn, så att
// Full diskåtkomst ges till just den här filen och inte till "Python".
//
//   notiser                 kör för evigt, kollar var åttonde sekund
//   notiser --en-gang       skriver läget och de fem senaste posterna
//
// Vilka appar som räknas står i buddy.json bredvid, nyckeln "notiser":
//   { "com.apple.MobileSMS": ["sms", "#4c8dff"], ... }
import Foundation
import SQLite3

let db_sokvag = NSString(string: "~/Library/Group Containers/group.com.apple.usernoted/db2/db").expandingTildeInPath
let brevlada = URL(string: "http://127.0.0.1:8739/")!
let har = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
let enGang = CommandLine.arguments.contains("--en-gang")

func logg(_ t: String) {
    let f = DateFormatter(); f.dateFormat = "HH:mm:ss"
    print("\(f.string(from: Date())) \(t)"); fflush(stdout)
}

/* Kartan app-id -> [typ, färg] ur buddy.json, med rimliga standardvärden. */
var karta: [String: [String]] = [
    "com.apple.MobileSMS": ["sms", "#4c8dff"],
    "com.microsoft.teams2": ["teams", "#6264a7"],
    "com.microsoft.teams": ["teams", "#6264a7"],
]
if let d = try? Data(contentsOf: har.appendingPathComponent("buddy.json")),
   let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any],
   let n = j["notiser"] as? [String: [String]] { karta = n }

func skicka(_ rad: String) {
    var req = URLRequest(url: brevlada); req.httpMethod = "POST"; req.httpBody = rad.data(using: .utf8)
    let sem = DispatchSemaphore(value: 0)
    URLSession.shared.dataTask(with: req) { _, _, fel in
        if let fel = fel { logg("brevlådan svarar inte: \(fel.localizedDescription)") } else { logg("→ \(rad)") }
        sem.signal()
    }.resume()
    _ = sem.wait(timeout: .now() + 3)
}

/* Letar efter en strängnyckel var som helst i en tolkad plist. */
func leta(_ v: Any, _ nyckel: String) -> String? {
    if let d = v as? [String: Any] {
        if let s = d[nyckel] as? String { return s }
        for x in d.values { if let s = leta(x, nyckel) { return s } }
    } else if let a = v as? [Any] {
        for x in a { if let s = leta(x, nyckel) { return s } }
    }
    return nil
}

func oppna() -> OpaquePointer? {
    var db: OpaquePointer?
    let uri = "file:\(db_sokvag)?mode=ro&immutable=1"
    if sqlite3_open_v2(uri, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nil) != SQLITE_OK {
        if db != nil { sqlite3_close(db) }
        return nil
    }
    /* Provläs, annars märks nekad åtkomst först vid frågan. */
    var st: OpaquePointer?
    if sqlite3_prepare_v2(db, "select count(*) from record", -1, &st, nil) != SQLITE_OK || sqlite3_step(st) != SQLITE_ROW {
        sqlite3_finalize(st); sqlite3_close(db); return nil
    }
    sqlite3_finalize(st)
    return db
}

struct Post { let id: Int64; let app: String; let text: String }

func nya(_ db: OpaquePointer, efter: Int64, max: Int) -> [Post] {
    var st: OpaquePointer?
    let sql = "select r.rec_id, a.identifier, r.data from record r join app a on a.app_id = r.app_id where r.rec_id > ? order by r.rec_id desc limit ?"
    guard sqlite3_prepare_v2(db, sql, -1, &st, nil) == SQLITE_OK else { return [] }
    sqlite3_bind_int64(st, 1, efter); sqlite3_bind_int(st, 2, Int32(max))
    var ut: [Post] = []
    while sqlite3_step(st) == SQLITE_ROW {
        let id = sqlite3_column_int64(st, 0)
        let app = sqlite3_column_text(st, 1).map { String(cString: $0) } ?? ""
        var text = ""
        if let p = sqlite3_column_blob(st, 2) {
            let n = Int(sqlite3_column_bytes(st, 2))
            let data = Data(bytes: p, count: n)
            if let pl = try? PropertyListSerialization.propertyList(from: data, format: nil) {
                let titel = leta(pl, "titl") ?? ""
                let kropp = (leta(pl, "body") ?? "").replacingOccurrences(of: "\n", with: " ")
                text = titel
                if !kropp.isEmpty { text += (text.isEmpty ? "" : ", ") + String(kropp.prefix(40)) }
            }
        }
        ut.append(Post(id: id, app: app, text: text))
    }
    sqlite3_finalize(st)
    return ut.reversed()
}

func maxId(_ db: OpaquePointer) -> Int64 {
    var st: OpaquePointer?; var m: Int64 = 0
    if sqlite3_prepare_v2(db, "select max(rec_id) from record", -1, &st, nil) == SQLITE_OK, sqlite3_step(st) == SQLITE_ROW { m = sqlite3_column_int64(st, 0) }
    sqlite3_finalize(st); return m
}

if enGang {
    guard let db = oppna() else { logg("ingen åtkomst till \(db_sokvag): ge den här filen Full diskåtkomst"); exit(2) }
    logg("läsbar, senaste rec_id \(maxId(db))")
    for p in nya(db, efter: maxId(db) - 5, max: 5) { logg("  \(p.id) \(p.app): \(p.text)") }
    exit(0)
}

var sista: Int64 = -1
var klagat = false
var tystTill: [String: Date] = [:]
logg("notiser startar, lyssnar efter \(karta.keys.sorted().joined(separator: ", "))")
while true {
    if let db = oppna() {
        if sista < 0 { sista = maxId(db); logg("läsbar, börjar vid rec_id \(sista)"); klagat = false }
        else {
            for p in nya(db, efter: sista, max: 20) {
                sista = max(sista, p.id)
                guard let tf = karta[p.app], let typ = tf.first else { continue }
                if let t = tystTill[typ], t > Date() { continue }
                let farg = tf.count > 1 ? tf[1] + " " : ""
                skicka("\(typ) \(farg)\(String(p.text.prefix(80)))")
                tystTill[typ] = Date().addingTimeInterval(20)
            }
        }
        sqlite3_close(db)
    } else if !klagat {
        logg("ingen åtkomst till notiscentralen: ge \(CommandLine.arguments[0]) Full diskåtkomst i Systeminställningar")
        klagat = true
    }
    sleep(8)
}
