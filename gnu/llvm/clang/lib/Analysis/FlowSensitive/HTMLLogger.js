//===-- HTMLLogger.js -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Based on selected objects, hide/show sections & populate data from templates.
//
// For example, if the selection is {bb="BB4", elt="BB4.6" iter="BB4:2"}:
//   - show the "block" and "element" sections
//   - re-render templates within these sections (if selection changed)
//   - apply "bb-select" to items with class class "BB4", etc
let selection = {};
function updateSelection(changes, data) {
  Object.assign(selection, changes);

  data = Object.create(data);
  data.selection = selection;
  for (root of document.querySelectorAll('[data-selection]'))
    updateSection(root, data);

  for (var k in changes)
    applyClassIf(k + '-select', classSelector(changes[k]));
}

// Given <section data-selection="x,y">:
//  - hide section if selections x or y are null
//  - re-render templates if x or y have changed
function updateSection(root, data) {
  let changed = root.selection == null;
  root.selection ||= {};
  for (key of root.dataset.selection.split(',')) {
    if (!key) continue;
    if (data.selection[key] != root.selection[key]) {
      root.selection[key] = data.selection[key];
      changed = true;
    }
    if (data.selection[key] == null) {
      root.hidden = true;
      return;
    }
  }
  if (changed) {
    root.hidden = false;
    for (tmpl of root.getElementsByTagName('template'))
      reinflate(tmpl, data);
  }
}

// Expands template `tmpl` based on input `data`:
//  - interpolates {{expressions}} in text and attributes
//  - <template> tags can modify expansion: if, for etc
// Outputs to `parent` element, inserting before `next`.
function inflate(tmpl, data, parent, next) {
  // We use eval() as our expression language in templates!
  // The templates are static and trusted.
  let evalExpr = (expr, data) => eval('with (data) { ' + expr + ' }');
  let interpolate = (str, data) =>
      str.replace(/\{\{(.*?)\}\}/g, (_, expr) => evalExpr(expr, data))
  // Anything other than <template> tag: copy, interpolate, recursively inflate.
  if (tmpl.nodeName != 'TEMPLATE') {
    let clone = tmpl.cloneNode();
    clone.inflated = true;
    if (clone instanceof Text)
      clone.textContent = interpolate(clone.textContent, data);
    if (clone instanceof Element) {
      for (attr of clone.attributes)
        attr.value = interpolate(attr.value, data);
      for (c of tmpl.childNodes)
        inflate(c, data, clone, /*next=*/null);
    }
    return parent.insertBefore(clone, next);
  }
  // data-use="xyz": use <template id="xyz"> instead. (Allows recursion.)
  if ('use' in tmpl.dataset)
    return inflate(document.getElementById(tmpl.dataset.use), data, parent, next);
  // <template> tag handling. Base case: recursively inflate.
  function handle(data) {
    for (c of tmpl.content.childNodes)
      inflate(c, data, parent, next);
  }
  // Directives on <template> tags modify behavior.
  const directives = {
    // data-for="x in expr": expr is enumerable, bind x to each in turn
    'for': (nameInExpr, data, proceed) => {
      let [name, expr] = nameInExpr.split(' in ');
      let newData = Object.create(data);
      let index = 0;
      for (val of evalExpr(expr, data) || []) {
        newData[name] = val;
        newData[name + '_index'] = index++;
        proceed(newData);
      }
    },
    // data-if="expr": only include contents if expression is truthy
    'if': (expr, data, proceed) => { if (evalExpr(expr, data)) proceed(data); },
    // data-let="x = expr": bind x to value of expr
    'let': (nameEqExpr, data, proceed) => {
      let [name, expr] = nameEqExpr.split(' = ');
      let newData = Object.create(data);
      newData[name] = evalExpr(expr, data);
      proceed(newData);
    },
  }
  // Compose directive handlers on top of the base handler.
  for (let [dir, value] of Object.entries(tmpl.dataset).reverse()) {
    if (dir in directives) {
      let proceed = handle;
      handle = (data) => directives[dir](value, data, proceed);
    }
  }
  handle(data);
}
// Expand a template, after first removing any prior expansion of it.
function reinflate(tmpl, data) {
  // Clear previously rendered template contents.
  while (tmpl.nextSibling && tmpl.nextSibling.inflated)
    tmpl.parentNode.removeChild(tmpl.nextSibling);
  inflate(tmpl, data, tmpl.parentNode, tmpl.nextSibling);
}

// Handle a mouse event on a region containing selectable items.
// This might end up changing the hover state or the selection state.
//
// targetSelector describes what target HTML element is selectable.
// targetToID specifies how to determine the selection from it:
//   hover: a function from target to the class name to highlight
//   bb: a function from target to the basic-block name to select (BB4)
//   elt: a function from target to the CFG element name to select (BB4.5)
//   iter: a function from target to the BB iteration to select (BB4:2)
// If an entry is missing, the selection is unmodified.
// If an entry is null, the selection is always cleared.
function mouseEventHandler(event, targetSelector, targetToID, data) {
  var target = event.type == "mouseout" ? null : event.target.closest(targetSelector);
  let selTarget = k => (target && targetToID[k]) ? targetToID[k](target) : null;
  if (event.type == "click") {
    let newSel = {};
    for (var k in targetToID) {
      if (k == 'hover') continue;
      let t = selTarget(k);
      newSel[k] = t;
    }
    updateSelection(newSel, data);
  } else if ("hover" in targetToID) {
    applyClassIf("hover", classSelector(selTarget("hover")));
  }
}
function watch(rootSelector, targetSelector, targetToID, data) {
  var root = document.querySelector(rootSelector);
  for (event of ['mouseout', 'mousemove', 'click'])
    root.addEventListener(event, e => mouseEventHandler(e, targetSelector, targetToID, data));
}
function watchSelection(data) {
  let lastIter = (bb) => `${bb}:${data.cfg[bb].iters}`;
  watch('#code', '.c', {
    hover: e => e.dataset.elt,
    bb: e => e.dataset.bb,
    elt: e => e.dataset.elt,
    // If we're already viewing an iteration of this BB, stick with the same.
    iter: e => (selection.iter && selection.bb == e.dataset.bb) ? selection.iter : lastIter(e.dataset.bb),
  }, data);
  watch('#cfg', '.bb', {
    hover: e => e.id,
    bb: e => e.id,
    elt: e => e.id + ".0",
    iter: e => lastIter(e.id),
  }, data);
  watch('#timeline', '.entry', {
    hover: e => [e.id, e.dataset.bb],
    bb: e => e.dataset.bb,
    elt: e => e.dataset.bb + ".0",
    iter: e => e.id,
  }, data);
  watch('#bb-elements', 'tr', {
    hover: e => e.id,
    elt: e => e.id,
  }, data);
  watch('#iterations', '.chooser', {
    hover: e => e.dataset.iter,
    iter: e => e.dataset.iter,
  }, data);
  updateSelection({}, data);
}
function applyClassIf(cls, query) {
  document.querySelectorAll('.' + cls).forEach(elt => elt.classList.remove(cls));
  document.querySelectorAll(query).forEach(elt => elt.classList.add(cls));
}
// Turns a class name into a CSS selector matching it, with some wrinkles:
// - we treat id="foo" just like class="foo" to avoid repetition in the HTML
// - cls can be an array of strings, we match them all
function classSelector(cls) {
  if (cls == null) return null;
  if (Array.isArray(cls)) return cls.map(classSelector).join(', ');
  var escaped = cls.replace('.', '\\.').replace(':', '\\:');
  // don't require id="foo" class="foo"
  return '.' + escaped + ", #" + escaped;
}

// Add a stylesheet defining colors for n basic blocks.
function addBBColors(n) {
  let sheet = new CSSStyleSheet();
  // hex values to subtract from fff to get a base color
  options = [0x001, 0x010, 0x011, 0x100, 0x101, 0x110, 0x111];
  function color(hex) {
    return "#" + hex.toString(16).padStart(3, "0");
  }
  function add(selector, property, hex) {
    sheet.insertRule(`${selector} { ${property}: ${color(hex)}; }`)
  }
  for (var i = 0; i < n; ++i) {
    let opt = options[i%options.length];
    add(`.B${i}`, 'background-color', 0xfff - 2*opt);
    add(`#B${i} polygon`, 'fill', 0xfff - 2*opt);
    add(`#B${i} polygon`, 'stroke', 0x888 - 4*opt);
  }
  document.adoptedStyleSheets.push(sheet);
}
