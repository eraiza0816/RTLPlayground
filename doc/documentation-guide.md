# Documentation guide

Type: explanation · How to write documentation for this project

This project organises its documentation with
[Diátaxis](https://diataxis.fr/): every document belongs to exactly one
of four modes, and its form follows from the need it serves.

## The four modes

| Mode | User need | Answers | Form |
|------|-----------|---------|------|
| **Tutorial** | learning | "How do I learn?" | A lesson: a guided, step-by-step path with checkpoints |
| **How-to** | a task | "How do I do X?" | A recipe: numbered steps to achieve a goal |
| **Reference** | information | "What is the fact?" | A description: objective, complete, no instructions |
| **Explanation** | understanding | "Why is it like this?" | The background, the concepts, the trade-offs |

A document has **one** primary mode.  A section that serves a different
need belongs in another document, linked from here.

## File layout and markers

- `doc/tutorials/` — tutorials
- `doc/how-to/` — how-to guides
- the remaining `doc/` files — reference and explanation (a physical
  split into `reference/` and `explanation/` is not done; use the
  `Type:` marker instead)
- every document starts with a one-line marker, for example:

  ```markdown
  Type: how-to · Task: install the firmware on a new switch
  ```

  or

  ```markdown
  Type: reference · Feature: the switch's ICMP echo sender
  ```

- link to a document by its file name, never by a heading.

## Templates

### Reference

Objective facts only: no "should", no advice, no encouragement.
Structure:

```
# <Feature>

Type: reference

One paragraph: what the feature is and what it does.

## Implementation     (hardware/registers, how the firmware drives it)
## CLI                (command syntax)
## WebUI              (panel / field description)
## JSON endpoint      (endpoint + example response)
## rtlpctl            (client commands)
## Notes              (caveats, defaults, persistence)
```

### How-to

One goal per document.  Structure:

```
# How-to: <task>

Type: how-to · Task: <goal>

## Prerequisites
## <numbered steps>   (do this, observe that)
## Next steps         (links)
```

### Explanation

The "why".  Structure:

```
# <Topic>

Type: explanation

The problem → the model → the design decisions → the implications.
Alternatives that were considered belong here.
```

### Tutorial

A lesson, not a task list: the user should learn by doing, and every
step ends with a checkpoint.

```
# Tutorial: <lesson>

Type: tutorial · Learning goal: <what the reader can do afterwards>

## Step 1 — ...   (with a "Checkpoint:" line at the end)
## Step 2 — ...
## You are done   (what was learned, where to go next)
```

## Practical rules

- Write in English (a few older documents are in Japanese; translate
  when touching them).
- The reference for a feature lives in one document (no duplicate
  command lists in the README).
- The README is the index: it groups the documents by mode and is
  updated whenever a document moves or is added.
