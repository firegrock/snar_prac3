function [q, integral, ok] = mahony_step(q, integral, f, omega, Kp, Ki, dt)
  % Original implementation, same explicit form as components/lab_core/core.c.
  % Hamilton q B->W; update integral first, then forward Euler q, normalize.
  q=q(:)'; integral=integral(:)'; f=f(:)'; omega=omega(:)'; ok=false;
  if !isfinite(dt) || dt<=0 || dt>0.05 || any(!isfinite(omega)), return; endif
  n=norm(f); e=[0,0,0];
  if all(isfinite(f)) && isfinite(n) && n>1e-6
    u=f/n;
    v=[2*(q(2)*q(4)-q(1)*q(3)), 2*(q(3)*q(4)+q(1)*q(2)), 1-2*(q(2)^2+q(3)^2)];
    e=cross(u,v);
  endif
  if Ki==0, integral=[0,0,0]; else integral=max(-0.5,min(0.5,integral+Ki*e*dt)); endif
  w=omega+Kp*e+integral;
  p=q+0.5*dt*[-dot(q(2:4),w), q(1)*w+cross(q(2:4),w)];
  n=norm(p);
  if !isfinite(n) || n<1e-6, q=[1,0,0,0]; integral=[0,0,0]; return; endif
  q=p/n; ok=true;
endfunction
