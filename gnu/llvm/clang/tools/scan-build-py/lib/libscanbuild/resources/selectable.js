function SetDisplay(RowClass, DisplayVal) {
  var Rows = document.getElementsByTagName("tr");
  for (var i = 0; i < Rows.length; ++i) {
    if (Rows[i].className == RowClass) {
      Rows[i].style.display = DisplayVal;
    }
  }
}

function CopyCheckedStateToCheckButtons(SummaryCheckButton) {
  var Inputs = document.getElementsByTagName("input");
  for (var i = 0; i < Inputs.length; ++i) {
    if (Inputs[i].type == "checkbox") {
      if (Inputs[i] != SummaryCheckButton) {
        Inputs[i].checked = SummaryCheckButton.checked;
        Inputs[i].onclick();
      }
    }
  }
}

function returnObjById(id) {
  if (document.getElementById)
    var returnVar = document.getElementById(id);
  else if (document.all)
    var returnVar = document.all[id];
  else if (document.layers)
    var returnVar = document.layers[id];
  return returnVar;
}

var NumUnchecked = 0;

function ToggleDisplay(CheckButton, ClassName) {
  if (CheckButton.checked) {
    SetDisplay(ClassName, "");
    if (--NumUnchecked == 0) {
      returnObjById("AllBugsCheck").checked = true;
    }
  } else {
    SetDisplay(ClassName, "none");
    NumUnchecked++;
    returnObjById("AllBugsCheck").checked = false;
  }
}
