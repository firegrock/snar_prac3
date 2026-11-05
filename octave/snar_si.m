function [f_B, omega_B, temperature] = snar_si(run)
  m = run.meta; s = run.samples;
  a_S = s(:,3:5) / m(2) * m(4);
  w_S = s(:,6:8) / m(3) * pi/180;
  a_S = bsxfun(@times, bsxfun(@minus, a_S, m(11:13)), m(14:16));
  if m(7), w_S = bsxfun(@minus, w_S, m(8:10)); endif
  R_BS = reshape(m(17:25), 3, 3)';
  if norm(R_BS*R_BS'-eye(3)) > 1e-8 || abs(det(R_BS)-1) > 1e-8
    error('R_BS is not a proper orthogonal rotation');
  endif
  f_B = a_S * R_BS'; omega_B = w_S * R_BS';
  temperature = s(:,9)/340 + 36.53;
endfunction
