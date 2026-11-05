function run = snar_load(directory)
  % GNU Octave 5.2+, base functions only. IDs remain in metadata.json, not doubles.
  run.samples = dlmread(fullfile(directory, 'samples.csv'), ',', 1, 0);
  run.meta = dlmread(fullfile(directory, 'metadata_numeric.csv'), ',', 1, 0);
  if columns(run.samples) != 14 || numel(run.meta) != 42
    error('Unexpected CSV schema');
  endif
  if isempty(run.samples), error('Empty recording'); endif
  if any(run.samples(:,2) > flintmax()), error('t_us exceeds exact double integer range'); endif
  run.t = (run.samples(:,2) - run.samples(1,2)) * 1e-6;
  run.seq = run.samples(:,1);
  run.flags = uint32(run.samples(:,14));
  run.segments = snar_check_time(run);
  [run.f_B, run.omega_B, run.temperature] = snar_si(run);
endfunction
