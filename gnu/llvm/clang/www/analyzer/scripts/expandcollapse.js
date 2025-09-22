// expand/collapse button (expander) is added if height of a cell content 
// exceeds CLIP_HEIGHT px.
var CLIP_HEIGHT = 135;

// Height in pixels of an expander image.
var EXPANDER_HEIGHT = 13;

// Path to images for an expander.
var imgPath = "./images/expandcollapse/";

// array[group][cell] of { 'height', 'expanded' }.
// group: a number; cells of the same group belong to the same table row.
// cell: a number; unique index of a cell in a group.
// height: a number, px; original height of a cell in a table.
// expanded: boolean; is a cell expanded or collapsed?
var CellsInfo = [];

// Extracts group and cell indices from an id of the form identifier_group_cell.
function getCellIdx(id) {
  var idx = id.substr(id.indexOf("_") + 1).split("_");
  return { 'group': idx[0], 'cell': idx[1] };
}

// Returns { 'height', 'expanded' } info for a cell with a given id.
function getCellInfo(id) { 
  var idx = getCellIdx(id); 
  return CellsInfo[idx.group][idx.cell]; 
}

// Initialization, add nodes, collect info.
function initExpandCollapse() {
  if (!document.getElementById)
    return;

  var groupCount = 0;

  // Examine all table rows in the document.
  var rows = document.body.getElementsByTagName("tr");
  for (var i=0; i<rows.length; i+=1) {

    var cellCount=0, newGroupCreated = false;

    // Examine all divs in a table row.
    var divs = rows[i].getElementsByTagName("div");
    for (var j=0; j<divs.length; j+=1) {

      var expandableDiv = divs[j];

      if (expandableDiv.className.indexOf("expandable") == -1)
        continue;

      if (expandableDiv.offsetHeight <= CLIP_HEIGHT)
        continue;

      // We found a div wrapping a cell content whose height exceeds 
      // CLIP_HEIGHT.
      var originalHeight = expandableDiv.offsetHeight;
      // Unique postfix for ids for generated nodes for a given cell.
      var idxStr = "_" + groupCount + "_" + cellCount;
      // Create an expander and an additional wrapper for a cell content.
      //
      //                                --- expandableDiv ----
      //  --- expandableDiv ---         | ------ data ------ |
      //  |    cell content   |   ->    | |  cell content  | | 
      //  ---------------------         | ------------------ |
      //                                | ---- expander ---- |
      //                                ----------------------
      var data = document.createElement("div");
      data.className = "data";
      data.id = "data" + idxStr;
      data.innerHTML = expandableDiv.innerHTML;
      with (data.style) { height = (CLIP_HEIGHT - EXPANDER_HEIGHT) + "px";
                          overflow = "hidden" }

      var expander = document.createElement("img");
      with (expander.style) { display = "block"; paddingTop = "5px"; }
      expander.src = imgPath + "ellipses_light.gif";
      expander.id = "expander" + idxStr;

      // Add mouse calbacks to expander.
      expander.onclick = function() {
        expandCollapse(this.id);
        // Hack for Opera - onmouseout callback is not invoked when page 
        // content changes dynamically and mouse pointer goes out of an element.
        this.src = imgPath + 
                   (getCellInfo(this.id).expanded ? "arrows_light.gif"
                                                  : "ellipses_light.gif");
      }
      expander.onmouseover = function() { 
        this.src = imgPath + 
                   (getCellInfo(this.id).expanded ? "arrows_dark.gif"
                                                  : "ellipses_dark.gif");
      }
      expander.onmouseout = function() { 
        this.src = imgPath + 
                   (getCellInfo(this.id).expanded ? "arrows_light.gif"
                                                  : "ellipses_light.gif");
      }

      expandableDiv.innerHTML = "";
      expandableDiv.appendChild(data);
      expandableDiv.appendChild(expander);
      expandableDiv.style.height = CLIP_HEIGHT + "px";
      expandableDiv.id = "cell"+ idxStr;

      // Keep original cell height and its ecpanded/cpllapsed state.
      if (!newGroupCreated) {
        CellsInfo[groupCount] = [];
        newGroupCreated = true;
      }
      CellsInfo[groupCount][cellCount] = { 'height' : originalHeight,
                                           'expanded' : false };
      cellCount += 1;
    }
    groupCount += newGroupCreated ? 1 : 0;
  }
}

function isElemTopVisible(elem) {
  var body = document.body,
      html = document.documentElement,
      // Calculate expandableDiv absolute Y coordinate from the top of body.
      bodyRect = body.getBoundingClientRect(),
      elemRect = elem.getBoundingClientRect(),
      elemOffset = Math.floor(elemRect.top - bodyRect.top),
      // Calculate the absoute Y coordinate of visible area.
      scrollTop = html.scrollTop || body && body.scrollTop || 0;
  scrollTop -= html.clientTop; // IE<8

  
  if (elemOffset < scrollTop)
    return false;

  return true;
}

// Invoked when an expander is pressed; expand/collapse a cell.
function expandCollapse(id) {
  var cellInfo = getCellInfo(id);
  var idx = getCellIdx(id);

  // New height of a row.
  var newHeight;
  // Smart page scrolling may be done after collapse.
  var mayNeedScroll;

  if (cellInfo.expanded) {
    // Cell is expanded - collapse the row height to CLIP_HEIGHT.
    newHeight = CLIP_HEIGHT;
    mayNeedScroll = true;
  }
  else {
    // Cell is collapsed - expand the row height to the cells original height.
    newHeight = cellInfo.height;
    mayNeedScroll = false;
  }

  // Update all cells (height and expanded/collapsed state) in a row according 
  // to the new height of the row.
  for (var i = 0; i < CellsInfo[idx.group].length; i++) {
    var idxStr = "_" + idx.group + "_" + i;
    var expandableDiv = document.getElementById("cell" + idxStr);
    expandableDiv.style.height = newHeight + "px";
    var data = document.getElementById("data" + idxStr);
    var expander = document.getElementById("expander" + idxStr);
    var state = CellsInfo[idx.group][i];

    if (state.height > newHeight) {
      // Cell height exceeds row height - collapse a cell.
      data.style.height = (newHeight - EXPANDER_HEIGHT) + "px";
      expander.src = imgPath + "ellipses_light.gif";
      CellsInfo[idx.group][i].expanded = false;
    } else {
      // Cell height is less then or equal to row height - expand a cell.
      data.style.height = "";
      expander.src = imgPath + "arrows_light.gif";
      CellsInfo[idx.group][i].expanded = true;
    }
  }

  if (mayNeedScroll) {
    var idxStr = "_" + idx.group + "_" + idx.cell;
    var clickedExpandableDiv = document.getElementById("cell" + idxStr);
    // Scroll page up if a row is collapsed and the rows top is above the 
    // viewport. The amount of scroll is the difference between a new and old 
    // row height.
    if (!isElemTopVisible(clickedExpandableDiv)) {
      window.scrollBy(0, newHeight - cellInfo.height);
    }
  }
}
