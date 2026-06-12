# Database Seed Commands for Care Schedules

This document provides copy-pasteable SQL insert commands and CLI commands to seed the `oro_base_care_schedules` table with the four care schedules.

## 1. System Constants (Used in the Seeding)
- **Dog ID**: `d0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0` (Buddy)
- **Device ID**: `9e092b69-5973-46e4-a228-fe4933e04364`
- **User ID**: `11111111-1111-1111-1111-111111111111` (developer@oropet.com)

---

## 2. CLI Command (Bash)
Run the following command directly in your terminal to insert all four care schedules into the database:

```bash
PGPASSWORD=ogmen psql -h localhost -U oro_user -d oro_base_db -c "
INSERT INTO oro_base_care_schedules (
    dog_id, device_id, care_type, title, description, recurrence_type, recurrence_days, scheduled_time, start_date, is_active, created_by_user_id
) VALUES
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'medication',
    'Daily Medication',
    'Give Buddy his daily medication.',
    'daily',
    NULL,
    '17:20:00'::time,
    CURRENT_DATE,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'grooming',
    'Weekly Grooming',
    'Brush Buddy''s coat.',
    'weekly',
    '[\"monday\"]'::jsonb,
    '17:20:00'::time,
    CURRENT_DATE,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'vaccination',
    'Monthly Vaccination Check',
    'Check Buddy''s vaccination record.',
    'monthly',
    NULL,
    '17:20:00'::time,
    '2026-06-08'::date,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'deworming',
    'Quarterly Deworming',
    'Administer Buddy''s quarterly deworming.',
    'monthly',
    NULL,
    '17:20:00'::time,
    '2026-06-08'::date,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
);
"
```

---

## 3. SQL Query
If you are logged into the `psql` console, you can run this query directly:

```sql
INSERT INTO oro_base_care_schedules (
    dog_id, device_id, care_type, title, description, recurrence_type, recurrence_days, scheduled_time, start_date, is_active, created_by_user_id
) VALUES
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'medication',
    'Daily Medication',
    'Give Buddy his daily medication.',
    'daily',
    NULL,
    '09:00:00'::time,
    CURRENT_DATE,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'grooming',
    'Weekly Grooming',
    'Brush Buddy''s coat.',
    'weekly',
    '["saturday"]'::jsonb,
    '10:00:00'::time,
    CURRENT_DATE,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'vaccination',
    'Monthly Vaccination Check',
    'Check Buddy''s vaccination record.',
    'monthly',
    NULL,
    '11:00:00'::time,
    '2026-06-08'::date,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
),
(
    'd0d0d0d0-d0d0-d0d0-d0d0-d0d0d0d0d0d0'::uuid,
    '9e092b69-5973-46e4-a228-fe4933e04364'::uuid,
    'deworming',
    'Quarterly Deworming',
    'Administer Buddy''s quarterly deworming.',
    'monthly',
    NULL,
    '12:00:00'::time,
    '2026-06-08'::date,
    true,
    '11111111-1111-1111-1111-111111111111'::uuid
);
```

---

## 4. Verification CLI Command
To verify that the schedules were successfully added, run:

```bash
PGPASSWORD=ogmen psql -h localhost -U oro_user -d oro_base_db -c "SELECT care_schedule_id, care_type, title, recurrence_type, scheduled_time, is_active FROM oro_base_care_schedules;"
```
