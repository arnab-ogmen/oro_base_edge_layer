#include "scheduled_task_manager/jobs/summary_jobs.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace oro::stm::jobs {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Build a human-readable date string like "12 Jun 2026" from a date string "2026-06-12".
static std::string friendly_date(const std::string& iso_date) {
    if (iso_date.size() < 10) return iso_date;
    int year  = std::stoi(iso_date.substr(0, 4));
    int month = std::stoi(iso_date.substr(5, 2));
    int day   = std::stoi(iso_date.substr(8, 2));
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
    std::ostringstream ss;
    ss << day << " " << months[month - 1] << " " << year;
    return ss.str();
}

static double safe_double(const pqxx::field& f) {
    return f.is_null() ? 0.0 : f.as<double>();
}

static int safe_int(const pqxx::field& f) {
    return f.is_null() ? 0 : f.as<int>();
}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare Statements
// ─────────────────────────────────────────────────────────────────────────────

void prepare_summary_job_statements(storage_handoff::StorageWriter& writer) {

    // Compute today's UTC period window using the configured IANA timezone.
    // $1 = timezone name (e.g. "Asia/Kolkata")
    // Returns: period_date (date text), period_start_utc, period_end_utc (timestamptz text)
    writer.prepare("stm_compute_daily_window", R"(
        SELECT
            (date_trunc('day', NOW() AT TIME ZONE $1))::date::text              AS period_date,
            (date_trunc('day', NOW() AT TIME ZONE $1) AT TIME ZONE $1)::text    AS period_start_utc,
            ((date_trunc('day', NOW() AT TIME ZONE $1) + INTERVAL '1 day')
             AT TIME ZONE $1)::text                                              AS period_end_utc
    )");

    // Compute period window for a specific date string (YYYY-MM-DD).
    // $1 = date string (e.g. '2026-06-11'), $2 = timezone name
    writer.prepare("stm_compute_window_for_date", R"(
        SELECT
            $1::date::text                                                          AS period_date,
            ($1::date::timestamp AT TIME ZONE $2)::text                             AS period_start_utc,
            (($1::date + INTERVAL '1 day')::timestamp AT TIME ZONE $2)::text        AS period_end_utc
    )");

    // Compute the past 7-day window using timezone.
    // $1 = timezone name
    // Returns proper ISO timestamps for week_start and week_end.
    writer.prepare("stm_compute_weekly_window", R"(
        SELECT
            (date_trunc('day', NOW() AT TIME ZONE $1) - INTERVAL '7 days')::date::text  AS week_start_date,
            (date_trunc('day', NOW() AT TIME ZONE $1))::date::text                       AS week_end_date,
            ((date_trunc('day', NOW() AT TIME ZONE $1) - INTERVAL '7 days')
             AT TIME ZONE $1)::text                                                      AS week_start_utc,
            (date_trunc('day', NOW() AT TIME ZONE $1) AT TIME ZONE $1)::text            AS week_end_utc
    )");

    // Check whether a summary already exists for this device/type/date.
    writer.prepare("stm_check_summary_exists", R"(
        SELECT COUNT(*) FROM public.oro_base_summary
        WHERE device_id = $1::uuid
          AND summary_type = $2
          AND summary_date = $3::date
    )");

    // Fetch the dog_id for the device.
    writer.prepare("stm_summary_get_dog_id", R"(
        SELECT dog_id FROM public.oro_base_device WHERE device_id = $1::uuid
    )");

    // Aggregate relevant signals for a device over a time window.
    // $1 = device_id, $2 = period_start (timestamptz), $3 = period_end (timestamptz)
    writer.prepare("stm_fetch_daily_signals", R"(
        SELECT
            AVG(CASE WHEN signal_id = 46  THEN signal_value_numeric END)  AS temp_avg,
            MIN(CASE WHEN signal_id = 46  THEN signal_value_numeric END)  AS temp_min,
            MAX(CASE WHEN signal_id = 46  THEN signal_value_numeric END)  AS temp_max,
            AVG(CASE WHEN signal_id = 48  THEN signal_value_numeric END)  AS humidity_avg,
            AVG(CASE WHEN signal_id = 47  THEN signal_value_numeric END)  AS light_avg,
            MIN(CASE WHEN signal_id = 47  THEN signal_value_numeric END)  AS light_min,
            MAX(CASE WHEN signal_id = 47  THEN signal_value_numeric END)  AS light_max,
            MIN(CASE WHEN signal_id = 73  THEN signal_value_numeric END)  AS battery_min,
            COALESCE(SUM(CASE WHEN signal_id = 117 THEN signal_value_numeric END), 0) AS food_served_g,
            COUNT(CASE WHEN signal_id = 117 THEN 1 END)                   AS meal_count,
            COALESCE(SUM(CASE WHEN signal_id = 125 THEN signal_value_numeric END), 0) AS treats_g,
            COUNT(CASE WHEN signal_id = 125 THEN 1 END)                   AS treat_count,
            COUNT(CASE WHEN signal_id = 71  THEN 1 END)                   AS heartbeat_count
        FROM public.oro_base_signals
        WHERE device_id = $1::uuid
          AND observed_at >= $2::timestamptz
          AND observed_at <  $3::timestamptz
    )");

    // Fetch all events for a device over a time window.
    writer.prepare("stm_fetch_daily_events", R"(
        SELECT event_id::text, event_type, severity, detected_at::text
        FROM public.oro_base_events
        WHERE device_id = $1::uuid
          AND detected_at >= $2::timestamptz
          AND detected_at <  $3::timestamptz
        ORDER BY detected_at ASC
    )");

    // Insert a daily OR weekly summary (ON CONFLICT DO NOTHING enforces idempotency
    // via the uix_summary_device_type_date unique index).
    // $1=device_id, $2=dog_id, $3=summary_type, $4=period_start, $5=period_end,
    // $6=summary_date, $7=title, $8=payload, $9=source_event_refs,
    // $10=source_signal_window, $11=generation_version
    writer.prepare("stm_insert_summary", R"(
        INSERT INTO public.oro_base_summary (
            device_id, dog_id, summary_type,
            period_start, period_end, summary_date,
            status, title, payload,
            generated_at, generation_version,
            source_event_refs, source_signal_window,
            sync_status
        )
        VALUES (
            $1::uuid, NULLIF($2, '')::uuid, $3,
            $4::timestamptz, $5::timestamptz, $6::date,
            'generated', $7, $8::jsonb,
            NOW(), $11,
            $9::jsonb, $10::jsonb,
            'pending'
        )
        ON CONFLICT DO NOTHING
    )");

    // Fetch the daily summaries for a device within a date range (for weekly rollup).
    // $1 = device_id, $2 = week_start (date), $3 = week_end (date, exclusive)
    writer.prepare("stm_fetch_week_daily_summaries", R"(
        SELECT summary_id::text, summary_date::text, payload
        FROM public.oro_base_summary
        WHERE device_id = $1::uuid
          AND summary_type = 'daily'
          AND summary_date >= $2::date
          AND summary_date <  $3::date
        ORDER BY summary_date ASC
    )");
}

// ─────────────────────────────────────────────────────────────────────────────
// Daily Pet Summary Generator
// ─────────────────────────────────────────────────────────────────────────────

JobResult daily_pet_summary_generator(const nlohmann::json& config,
                                      storage_handoff::StorageWriter& writer) {
    JobResult res;
    res.success = true;
    res.items_processed = 0;

    std::string device_id = config.value("/global/device_id"_json_pointer, "");
    std::string timezone  = config.value("/global/timezone"_json_pointer, "UTC");
    // Optional date override from --date YYYY-MM-DD argument
    std::string run_date  = config.value("run_date", "");

    if (device_id.empty()) {
        res.success = false; res.error = "device_id not configured"; return res;
    }

    std::cout << "[SummaryJobs] daily_pet_summary_generator starting."
              << " tz=" << timezone;
    if (!run_date.empty()) {
        std::cout << " date_override=" << run_date;
    }
    std::cout << "\n";

    // 1. Compute period window — use specific date if provided, otherwise "today"
    pqxx::result window_result;
    if (run_date.empty()) {
        window_result = writer.query_result("stm_compute_daily_window", timezone);
    } else {
        window_result = writer.query_result("stm_compute_window_for_date", run_date, timezone);
    }

    if (window_result.empty()) {
        res.success = false; res.error = "Window computation failed"; return res;
    }
    std::string period_date      = window_result[0]["period_date"].as<std::string>();
    std::string period_start_utc = window_result[0]["period_start_utc"].as<std::string>();
    std::string period_end_utc   = window_result[0]["period_end_utc"].as<std::string>();

    std::cout << "[SummaryJobs] Daily window: " << period_start_utc
              << " → " << period_end_utc << "\n";

    // 2. Idempotency check
    auto exists = writer.query_result("stm_check_summary_exists", device_id, "daily", period_date);
    if (!exists.empty() && exists[0][0].as<int>() > 0) {
        std::cout << "[SummaryJobs] Daily summary for " << period_date << " already exists. Skipping.\n";
        res.metadata["skipped"] = true;
        res.metadata["reason"]  = "already_exists";
        res.metadata["date"]    = period_date;
        return res;
    }

    // 3. Dog ID
    std::string dog_id = "";
    auto dog_result = writer.query_result("stm_summary_get_dog_id", device_id);
    if (!dog_result.empty() && !dog_result[0][0].is_null()) {
        dog_id = dog_result[0][0].as<std::string>();
    }

    // 4. Aggregate signals
    auto sig = writer.query_result("stm_fetch_daily_signals",
                                   device_id, period_start_utc, period_end_utc);

    nlohmann::json payload = nlohmann::json::object();
    if (!sig.empty()) {
        auto& row = sig[0];
        auto set_nullable = [&](const char* key, const pqxx::field& f) {
            if (!f.is_null()) payload[key] = safe_double(f);
        };
        set_nullable("temperature_avg_c",  row["temp_avg"]);
        set_nullable("temperature_min_c",  row["temp_min"]);
        set_nullable("temperature_max_c",  row["temp_max"]);
        set_nullable("humidity_avg_pct",   row["humidity_avg"]);
        set_nullable("light_avg_lux",      row["light_avg"]);
        set_nullable("light_min_lux",      row["light_min"]);
        set_nullable("light_max_lux",      row["light_max"]);
        set_nullable("battery_min_pct",    row["battery_min"]);
        payload["food_served_g"]      = safe_double(row["food_served_g"]);
        payload["meal_count"]         = safe_int(row["meal_count"]);
        payload["treats_dispensed_g"] = safe_double(row["treats_g"]);
        payload["treat_count"]        = safe_int(row["treat_count"]);
        payload["heartbeat_count"]    = safe_int(row["heartbeat_count"]);
    }

    // 5. Aggregate events
    auto ev = writer.query_result("stm_fetch_daily_events",
                                  device_id, period_start_utc, period_end_utc);

    nlohmann::json source_event_refs = nlohmann::json::array();
    int high_sev = 0, overdue = 0, offline = 0, stale = 0, supply = 0;

    for (const auto& row : ev) {
        source_event_refs.push_back(row["event_id"].as<std::string>());
        std::string evt  = row["event_type"].as<std::string>();
        std::string sev  = row["severity"].is_null() ? "" : row["severity"].as<std::string>();
        if (sev == "high" || sev == "critical") high_sev++;
        if (evt == "feeding_overdue")   overdue++;
        if (evt == "device_offline")    offline++;
        if (evt == "sensor_data_stale") stale++;
        if (evt == "low_supply")        supply++;
    }

    payload["high_severity_event_count"] = high_sev;
    payload["feeding_overdue_count"]     = overdue;
    payload["device_offline_count"]      = offline;
    payload["stale_sensor_count"]        = stale;
    payload["supply_alert_count"]        = supply;

    // 6. Build and insert
    std::string title = "Daily Summary – " + friendly_date(period_date);
    nlohmann::json signal_window = {{"from", period_start_utc}, {"to", period_end_utc}};

    bool ok = writer.execute_prepared("stm_insert_summary",
        device_id, dog_id,
        std::string("daily"),
        period_start_utc, period_end_utc, period_date,
        title,
        payload.dump(),
        source_event_refs.dump(),
        signal_window.dump(),
        std::string("1.0"));

    if (!ok) {
        res.success = false; res.error = "Insert failed"; return res;
    }

    res.items_processed = 1;
    res.metadata["date"]          = period_date;
    res.metadata["period_start"]  = period_start_utc;
    res.metadata["period_end"]    = period_end_utc;
    res.metadata["events_logged"] = (int)source_event_refs.size();

    std::cout << "[SummaryJobs] Daily summary written for " << period_date
              << " (events=" << source_event_refs.size()
              << ", heartbeats=" << payload.value("heartbeat_count", 0)
              << ", food=" << payload.value("food_served_g", 0.0) << "g).\n";
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// Weekly Pet Summary Generator
// ─────────────────────────────────────────────────────────────────────────────

JobResult weekly_pet_summary_generator(const nlohmann::json& config,
                                       storage_handoff::StorageWriter& writer) {
    JobResult res;
    res.success = true;
    res.items_processed = 0;

    std::string device_id = config.value("/global/device_id"_json_pointer, "");
    std::string timezone  = config.value("/global/timezone"_json_pointer, "UTC");
    if (device_id.empty()) {
        res.success = false; res.error = "device_id not configured"; return res;
    }

    std::cout << "[SummaryJobs] weekly_pet_summary_generator starting. tz=" << timezone << "\n";

    // 1. Compute the 7-day window with proper timezone-aware timestamps from DB
    auto w = writer.query_result("stm_compute_weekly_window", timezone);
    if (w.empty()) {
        res.success = false; res.error = "Weekly window computation failed"; return res;
    }
    std::string week_start_date = w[0]["week_start_date"].as<std::string>();
    std::string week_end_date   = w[0]["week_end_date"].as<std::string>();
    std::string week_start_utc  = w[0]["week_start_utc"].as<std::string>();
    std::string week_end_utc    = w[0]["week_end_utc"].as<std::string>();

    std::cout << "[SummaryJobs] Weekly window: " << week_start_utc
              << " → " << week_end_utc << "\n";

    // 2. Idempotency: key off week_start_date (start of the 7-day period)
    auto exists = writer.query_result("stm_check_summary_exists",
                                      device_id, "weekly", week_start_date);
    if (!exists.empty() && exists[0][0].as<int>() > 0) {
        std::cout << "[SummaryJobs] Weekly summary for week starting "
                  << week_start_date << " already exists. Skipping.\n";
        res.metadata["skipped"]          = true;
        res.metadata["reason"]           = "already_exists";
        res.metadata["week_start_date"]  = week_start_date;
        return res;
    }

    // 3. Dog ID
    std::string dog_id = "";
    auto dog = writer.query_result("stm_summary_get_dog_id", device_id);
    if (!dog.empty() && !dog[0][0].is_null()) {
        dog_id = dog[0][0].as<std::string>();
    }

    // 4. Fetch the 7 daily summaries
    auto daily_rows = writer.query_result("stm_fetch_week_daily_summaries",
                                          device_id, week_start_date, week_end_date);
    int daily_count = (int)daily_rows.size();
    std::cout << "[SummaryJobs] Found " << daily_count << "/7 daily summaries for week.\n";

    if (daily_count == 0) {
        std::cout << "[SummaryJobs] No daily summaries found. Nothing to aggregate.\n";
        res.metadata["skipped"] = true;
        res.metadata["reason"]  = "no_daily_summaries";
        return res;
    }

    // 5. Aggregate from daily payloads
    double total_food_g = 0, total_treats_g = 0;
    double sum_temp = 0, sum_battery = 0;
    int    total_meals = 0, total_treats = 0;
    int    total_high_ev = 0, total_overdue = 0, total_offline = 0;
    int    days_with_data = 0;
    nlohmann::json source_summary_refs = nlohmann::json::array();

    for (const auto& row : daily_rows) {
        source_summary_refs.push_back(row["summary_id"].as<std::string>());
        if (row["payload"].is_null()) continue;
        auto p = nlohmann::json::parse(row["payload"].as<std::string>(),
                                       nullptr, /*allow_exceptions=*/false);
        if (p.is_discarded()) continue;

        days_with_data++;
        total_food_g   += p.value("food_served_g",              0.0);
        total_treats_g += p.value("treats_dispensed_g",         0.0);
        total_meals    += p.value("meal_count",                  0);
        total_treats   += p.value("treat_count",                 0);
        total_high_ev  += p.value("high_severity_event_count",   0);
        total_overdue  += p.value("feeding_overdue_count",       0);
        total_offline  += p.value("device_offline_count",        0);
        if (p.contains("temperature_avg_c") && !p["temperature_avg_c"].is_null())
            sum_temp    += p["temperature_avg_c"].get<double>();
        if (p.contains("battery_min_pct") && !p["battery_min_pct"].is_null())
            sum_battery += p["battery_min_pct"].get<double>();
    }

    // 6. Build weekly payload
    nlohmann::json payload = {
        {"daily_summaries_count",           daily_count},
        {"days_with_data",                  days_with_data},
        {"food_served_g_weekly_total",      total_food_g},
        {"treats_dispensed_g_weekly_total", total_treats_g},
        {"total_meal_count",                total_meals},
        {"total_treat_count",               total_treats},
        {"total_high_severity_events",      total_high_ev},
        {"total_feeding_overdue_count",     total_overdue},
        {"total_device_offline_count",      total_offline}
    };
    if (days_with_data > 0) {
        payload["avg_temperature_avg_c"] = sum_temp    / days_with_data;
        payload["avg_battery_min_pct"]   = sum_battery / days_with_data;
    }

    // 7. signal_window uses proper ISO timestamps (same format as daily)
    nlohmann::json signal_window = {
        {"from", week_start_utc},
        {"to",   week_end_utc},
        {"type", "weekly_from_daily_summaries"}
    };

    std::string title = "Weekly Summary – " + friendly_date(week_start_date)
                        + " to " + friendly_date(week_end_date);

    // summary_date = week_start_date (start of the 7-day window)
    bool ok = writer.execute_prepared("stm_insert_summary",
        device_id, dog_id,
        std::string("weekly"),
        week_start_utc, week_end_utc,
        week_start_date,   // ← summary_date = start of the week
        title,
        payload.dump(),
        source_summary_refs.dump(),
        signal_window.dump(),
        std::string("1.0"));

    if (!ok) {
        res.success = false; res.error = "Insert failed"; return res;
    }

    res.items_processed = 1;
    res.metadata["week_start"]      = week_start_date;
    res.metadata["week_end"]        = week_end_date;
    res.metadata["daily_summaries"] = daily_count;
    res.metadata["total_food_g"]    = total_food_g;

    std::cout << "[SummaryJobs] Weekly summary written. Period: "
              << week_start_utc << " → " << week_end_utc
              << ", based on " << daily_count << " daily summaries.\n";
    return res;
}

} // namespace oro::stm::jobs
