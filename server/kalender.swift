// Kalendern — skriver kommande händelser som JSON-rader, en per händelse.
//
//   ./kalender 90                 händelser som börjar inom 90 minuter (standard 60)
//   ./kalender paminnelser 5      påminnelser som förfallit de senaste 5 minuterna
//                                 eller förfaller inom 5, ej avklarade
//
// Läser macOS egna kalenderdatabas via EventKit, alltså alla konton som
// ligger i Kalender-appen: iCloud, Google, Exchange. Första gången frågar
// macOS om tillstånd; det måste godkännas för att något ska komma ut.
import EventKit
import Foundation

let arg = CommandLine.arguments
let paminnelser = arg.count > 1 && arg[1] == "paminnelser"
let minuter = Double(arg.count > (paminnelser ? 2 : 1) ? arg[paminnelser ? 2 : 1] : (paminnelser ? "5" : "60")) ?? 60
let butik = EKEventStore()
let sem = DispatchSemaphore(value: 0)
var ok = false
let iso = ISO8601DateFormatter()
iso.timeZone = TimeZone.current
let nu = Date()

if paminnelser {
    if #available(macOS 14.0, *) {
        butik.requestFullAccessToReminders { granted, _ in ok = granted; sem.signal() }
    } else {
        butik.requestAccess(to: .reminder) { granted, _ in ok = granted; sem.signal() }
    }
    sem.wait()
    guard ok else {
        FileHandle.standardError.write("ingen tillgång till påminnelserna: godkänn i Systeminställningar > Integritet > Påminnelser\n".data(using: .utf8)!)
        exit(2)
    }
    /* Ej avklarade påminnelser med förfallotid inom fönstret, bakåt och framåt. */
    let fran = nu.addingTimeInterval(-minuter * 60)
    let till = nu.addingTimeInterval(minuter * 60)
    let filter = butik.predicateForIncompleteReminders(withDueDateStarting: fran, ending: till, calendars: nil)
    var lista: [EKReminder] = []
    butik.fetchReminders(matching: filter) { r in lista = r ?? []; sem.signal() }
    sem.wait()
    for p in lista {
        guard let dc = p.dueDateComponents, let due = Calendar.current.date(from: dc) else { continue }
        let kvar = Int((due.timeIntervalSince(nu) / 60).rounded())
        let rad: [String: Any] = [
            "id": p.calendarItemIdentifier,
            "titel": p.title ?? "",
            "lista": p.calendar.title,
            "forfaller": iso.string(from: due),
            "minuter": kvar,
        ]
        if let d = try? JSONSerialization.data(withJSONObject: rad), let s = String(data: d, encoding: .utf8) {
            print(s)
        }
    }
    exit(0)
}

if #available(macOS 14.0, *) {
    butik.requestFullAccessToEvents { granted, _ in ok = granted; sem.signal() }
} else {
    butik.requestAccess(to: .event) { granted, _ in ok = granted; sem.signal() }
}
sem.wait()

guard ok else {
    FileHandle.standardError.write("ingen tillgång till kalendern: godkänn i Systeminställningar > Integritet > Kalendrar\n".data(using: .utf8)!)
    exit(2)
}

let slut = nu.addingTimeInterval(minuter * 60)
let filter = butik.predicateForEvents(withStart: nu.addingTimeInterval(-15 * 60), end: slut, calendars: nil)

func typnamn(_ t: EKCalendarType) -> String {
    switch t {
    case .local: return "local"
    case .calDAV: return "caldav"
    case .exchange: return "exchange"
    case .subscription: return "subscription"
    case .birthday: return "birthday"
    @unknown default: return "other"
    }
}

for h in butik.events(matching: filter).sorted(by: { $0.startDate < $1.startDate }) {
    if h.isAllDay { continue }
    let kvar = Int((h.startDate.timeIntervalSince(nu) / 60).rounded())
    let rad: [String: Any] = [
        "id": h.eventIdentifier ?? "",
        "titel": h.title ?? "",
        "kalender": h.calendar.title,
        "konto": h.calendar.source.title,
        "typ": typnamn(h.calendar.type),
        "start": iso.string(from: h.startDate),
        "minuter": kvar,
    ]
    if let d = try? JSONSerialization.data(withJSONObject: rad), let s = String(data: d, encoding: .utf8) {
        print(s)
    }
}
