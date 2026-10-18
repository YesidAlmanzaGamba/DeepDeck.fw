# Plans

One file per piece of work, named `YYYY-MM-DD-<topic>.md`, written before implementation starts.
`ROADMAP.md` lists everything proposed and links to the plan of each item once it exists.

## Statuses

`proposed` → `approved` → `in progress` → `done` (or `dropped`, with the reason)

## Template

```markdown
# <Title>

- **Status:** proposed | approved | in progress | done | dropped
- **Roadmap item:** <id from ROADMAP.md>
- **Areas:** firmware | web | both

## Goal
What should be true when this is done, and why it matters.

## Scope
In scope / out of scope.

## Design
How it will work. API or storage changes (with migration), affected files.

## Steps
1. ... (each step builds on its own)

## Verification
How it is checked: firmware build, web build, and what to test on the device.

## Result
Filled in when done: commits, what was verified, anything left open (also in docs/traceability/CHANGELOG.md).
```
